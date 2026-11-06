#include "tca/journal.hpp"

#include <map>
#include <set>

namespace tca {

namespace {

const std::map<std::string, std::set<std::string>>& transitions() {
  static const std::map<std::string, std::set<std::string>> t{
      {"PROPOSED", {"REVIEWED", "REJECTED", "CANCELLED"}},
      {"REVIEWED", {"APPROVED_BY_RISK", "REJECTED", "CANCELLED"}},
      {"APPROVED_BY_RISK", {"SUBMITTED", "REJECTED", "CANCELLED", "UNKNOWN"}},
      {"SUBMITTED", {"ACKNOWLEDGED", "PARTIALLY_FILLED", "FILLED", "REJECTED", "CANCEL_PENDING", "UNKNOWN"}},
      {"ACKNOWLEDGED", {"PARTIALLY_FILLED", "FILLED", "CANCEL_PENDING", "CANCELLED", "REJECTED", "UNKNOWN"}},
      {"PARTIALLY_FILLED", {"PARTIALLY_FILLED", "FILLED", "CANCEL_PENDING", "CANCELLED", "UNKNOWN"}},
      {"CANCEL_PENDING", {"CANCELLED", "FILLED", "PARTIALLY_FILLED", "UNKNOWN"}},
      {"UNKNOWN", {"ACKNOWLEDGED", "PARTIALLY_FILLED", "FILLED", "CANCELLED", "REJECTED"}},
  };
  return t;
}

struct Stmt {
  sqlite3_stmt* s = nullptr;
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
  }
  ~Stmt() { sqlite3_finalize(s); }
  Stmt& text(int i, const std::string& v) { sqlite3_bind_text(s, i, v.c_str(), -1, SQLITE_TRANSIENT); return *this; }
  Stmt& num(int i, double v) { sqlite3_bind_double(s, i, v); return *this; }
  Stmt& i64(int i, int64_t v) { sqlite3_bind_int64(s, i, v); return *this; }
  Stmt& null(int i) { sqlite3_bind_null(s, i); return *this; }
  int step() { return sqlite3_step(s); }
  std::string col_text(int i) { auto p = sqlite3_column_text(s, i); return p ? reinterpret_cast<const char*>(p) : ""; }
  double col_num(int i) { return sqlite3_column_double(s, i); }
  int64_t col_i64(int i) { return sqlite3_column_int64(s, i); }
};

}  // namespace

bool valid_order_transition(const std::string& from, const std::string& to) {
  auto it = transitions().find(from);
  return it != transitions().end() && it->second.count(to) > 0;
}

bool is_terminal_state(const std::string& s) { return s == "FILLED" || s == "CANCELLED" || s == "REJECTED"; }

Journal::Journal(const std::string& path) {
  if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) throw std::runtime_error("cannot open journal db: " + path);
  sqlite3_busy_timeout(db_, 5000);
  exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;");
}

Journal::~Journal() { sqlite3_close(db_); }

void Journal::exec(const std::string& sql) {
  char* err = nullptr;
  if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
    std::string e = err ? err : "sqlite error";
    sqlite3_free(err);
    throw std::runtime_error(e);
  }
}

void Journal::init() {
  exec(R"SQL(
CREATE TABLE IF NOT EXISTS orders(
  client_order_id TEXT PRIMARY KEY, mode TEXT NOT NULL, intent_key TEXT NOT NULL,
  symbol TEXT NOT NULL, asset_class TEXT NOT NULL, side TEXT NOT NULL,
  qty REAL NOT NULL, limit_price REAL, stop REAL, target REAL, strategy TEXT,
  state TEXT NOT NULL, broker_order_id TEXT UNIQUE, filled_qty REAL NOT NULL DEFAULT 0,
  avg_fill_price REAL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL,
  UNIQUE(mode, intent_key));
CREATE TABLE IF NOT EXISTS order_events(
  id INTEGER PRIMARY KEY AUTOINCREMENT, client_order_id TEXT NOT NULL REFERENCES orders(client_order_id),
  from_state TEXT, to_state TEXT NOT NULL, at INTEGER NOT NULL, detail TEXT);
CREATE TABLE IF NOT EXISTS fills(
  id INTEGER PRIMARY KEY AUTOINCREMENT, client_order_id TEXT NOT NULL REFERENCES orders(client_order_id),
  fill_id TEXT NOT NULL UNIQUE, qty REAL NOT NULL, price REAL NOT NULL, at INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS trades(
  id INTEGER PRIMARY KEY AUTOINCREMENT, mode TEXT NOT NULL, symbol TEXT NOT NULL, strategy TEXT,
  qty REAL NOT NULL, entry REAL NOT NULL, exit REAL NOT NULL, pnl REAL NOT NULL, r_multiple REAL,
  opened_at INTEGER, closed_at INTEGER NOT NULL, exit_reason TEXT, entry_order_id TEXT UNIQUE);
CREATE TABLE IF NOT EXISTS audit(
  id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER NOT NULL, kind TEXT NOT NULL, detail TEXT);
CREATE TABLE IF NOT EXISTS kv(key TEXT PRIMARY KEY, value TEXT NOT NULL);
)SQL");
}

json Journal::propose(const json& o, int64_t now) {
  const std::string id = get_str(o, "client_order_id");
  const std::string mode = get_str(o, "mode");
  if (mode != "PAPER" && mode != "LIVE") throw ValidationError("orders may only be journaled in PAPER or LIVE mode");
  const double qty = get_num(o, "qty");
  if (qty <= 0) throw ValidationError("qty must be positive");
  Stmt s(db_, "INSERT INTO orders(client_order_id,mode,intent_key,symbol,asset_class,side,qty,limit_price,stop,target,"
              "strategy,state,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,'PROPOSED',?,?)");
  s.text(1, id).text(2, mode).text(3, get_str(o, "intent_key")).text(4, get_str(o, "symbol"))
      .text(5, get_str(o, "asset_class")).text(6, get_str_or(o, "side", "buy")).num(7, qty)
      .num(8, get_num_or(o, "limit_price", 0)).num(9, get_num_or(o, "stop", 0)).num(10, get_num_or(o, "target", 0))
      .text(11, get_str_or(o, "strategy", "")).i64(12, now).i64(13, now);
  if (s.step() != SQLITE_DONE) {
    std::string e = sqlite3_errmsg(db_);
    if (e.find("UNIQUE") != std::string::npos) throw ValidationError("duplicate order intent rejected: " + e);
    throw std::runtime_error(e);
  }
  Stmt ev(db_, "INSERT INTO order_events(client_order_id,from_state,to_state,at,detail) VALUES(?,NULL,'PROPOSED',?,?)");
  ev.text(1, id).i64(2, now).text(3, o.dump());
  ev.step();
  return json{{"client_order_id", id}, {"state", "PROPOSED"}};
}

json Journal::transition(const std::string& id, const std::string& to, int64_t now, const json& detail) {
  exec("BEGIN IMMEDIATE");
  try {
    Stmt q(db_, "SELECT state FROM orders WHERE client_order_id=?");
    q.text(1, id);
    if (q.step() != SQLITE_ROW) throw ValidationError("unknown client_order_id: " + id);
    const std::string from = q.col_text(0);
    if (!valid_order_transition(from, to)) throw ValidationError("illegal order transition " + from + " -> " + to);
    Stmt u(db_, "UPDATE orders SET state=?, updated_at=?, broker_order_id=COALESCE(?, broker_order_id) WHERE client_order_id=?");
    u.text(1, to).i64(2, now);
    if (detail.contains("broker_order_id")) u.text(3, detail["broker_order_id"].get<std::string>()); else u.null(3);
    u.text(4, id);
    if (u.step() != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    Stmt ev(db_, "INSERT INTO order_events(client_order_id,from_state,to_state,at,detail) VALUES(?,?,?,?,?)");
    ev.text(1, id).text(2, from).text(3, to).i64(4, now).text(5, detail.dump());
    ev.step();
    exec("COMMIT");
    return json{{"client_order_id", id}, {"from", from}, {"state", to}};
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
}

json Journal::record_fill(const std::string& id, double qty, double price, const std::string& fill_id, int64_t now) {
  if (qty <= 0 || price <= 0) throw ValidationError("fill qty/price must be positive");
  exec("BEGIN IMMEDIATE");
  try {
    Stmt q(db_, "SELECT qty, filled_qty, COALESCE(avg_fill_price,0) FROM orders WHERE client_order_id=?");
    q.text(1, id);
    if (q.step() != SQLITE_ROW) throw ValidationError("unknown client_order_id: " + id);
    const double oq = q.col_num(0), fq = q.col_num(1), ap = q.col_num(2);
    if (fq + qty > oq + 1e-9) throw ValidationError("fill would exceed order quantity");
    Stmt f(db_, "INSERT INTO fills(client_order_id,fill_id,qty,price,at) VALUES(?,?,?,?,?)");
    f.text(1, id).text(2, fill_id).num(3, qty).num(4, price).i64(5, now);
    if (f.step() != SQLITE_DONE) throw ValidationError(std::string("duplicate/invalid fill: ") + sqlite3_errmsg(db_));
    const double nq = fq + qty, nap = (ap * fq + price * qty) / nq;
    Stmt u(db_, "UPDATE orders SET filled_qty=?, avg_fill_price=?, updated_at=? WHERE client_order_id=?");
    u.num(1, nq).num(2, nap).i64(3, now).text(4, id);
    u.step();
    exec("COMMIT");
    return json{{"client_order_id", id}, {"filled_qty", nq}, {"avg_fill_price", nap}, {"complete", nq >= oq - 1e-9}};
  } catch (...) {
    exec("ROLLBACK");
    throw;
  }
}

json Journal::close_trade(const json& t, int64_t now) {
  Stmt s(db_, "INSERT INTO trades(mode,symbol,strategy,qty,entry,exit,pnl,r_multiple,opened_at,closed_at,exit_reason,"
              "entry_order_id) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
  s.text(1, get_str(t, "mode")).text(2, get_str(t, "symbol")).text(3, get_str_or(t, "strategy", ""))
      .num(4, get_num(t, "qty")).num(5, get_num(t, "entry")).num(6, get_num(t, "exit")).num(7, get_num(t, "pnl"))
      .num(8, get_num_or(t, "r_multiple", 0)).i64(9, static_cast<int64_t>(get_num_or(t, "opened_at", 0))).i64(10, now)
      .text(11, get_str_or(t, "exit_reason", "")).text(12, get_str(t, "entry_order_id"));
  if (s.step() != SQLITE_DONE) throw ValidationError(std::string("cannot record trade: ") + sqlite3_errmsg(db_));
  return json{{"recorded", true}};
}

void Journal::audit(const std::string& kind, const json& detail, int64_t now) {
  Stmt s(db_, "INSERT INTO audit(at,kind,detail) VALUES(?,?,?)");
  s.i64(1, now).text(2, kind).text(3, detail.dump());
  s.step();
}

json Journal::open_orders(const std::string& mode) {
  Stmt s(db_, "SELECT client_order_id,symbol,state,qty,filled_qty,limit_price,broker_order_id,updated_at FROM orders "
              "WHERE mode=? AND state NOT IN ('FILLED','CANCELLED','REJECTED') ORDER BY created_at");
  s.text(1, mode);
  json arr = json::array();
  while (s.step() == SQLITE_ROW)
    arr.push_back({{"client_order_id", s.col_text(0)}, {"symbol", s.col_text(1)}, {"state", s.col_text(2)},
                   {"qty", s.col_num(3)}, {"filled_qty", s.col_num(4)}, {"limit_price", s.col_num(5)},
                   {"broker_order_id", s.col_text(6)}, {"updated_at", s.col_i64(7)}});
  return arr;
}

int Journal::unknown_count(const std::string& mode) {
  Stmt s(db_, "SELECT COUNT(*) FROM orders WHERE mode=? AND state='UNKNOWN'");
  s.text(1, mode);
  s.step();
  return static_cast<int>(s.col_i64(0));
}

json Journal::report(const std::string& mode, int64_t since) {
  Stmt s(db_, "SELECT symbol,strategy,qty,entry,exit,pnl,r_multiple,opened_at,closed_at,exit_reason FROM trades "
              "WHERE mode=? AND closed_at>=? ORDER BY closed_at");
  s.text(1, mode).i64(2, since);
  json trades = json::array();
  double net = 0; int wins = 0;
  json by = json::object();
  while (s.step() == SQLITE_ROW) {
    json t{{"symbol", s.col_text(0)}, {"strategy", s.col_text(1)}, {"qty", s.col_num(2)}, {"entry", s.col_num(3)},
           {"exit", s.col_num(4)}, {"pnl", s.col_num(5)}, {"r_multiple", s.col_num(6)}, {"opened_at", s.col_i64(7)},
           {"closed_at", s.col_i64(8)}, {"exit_reason", s.col_text(9)}};
    net += s.col_num(5); wins += s.col_num(5) > 0;
    json& b = by[s.col_text(1)];
    if (!b.is_object()) b = json::object();
    b["trades"] = b.value("trades", 0) + 1;
    b["net_pnl"] = b.value("net_pnl", 0.0) + s.col_num(5);
    trades.push_back(t);
  }
  return json{{"mode", mode}, {"since", since}, {"trades", trades}, {"count", trades.size()}, {"net_pnl", net},
              {"win_rate", trades.empty() ? 0.0 : static_cast<double>(wins) / trades.size()},
              {"by_strategy", by}, {"open_orders", open_orders(mode)}, {"unknown_orders", unknown_count(mode)}};
}

void Journal::kv_set(const std::string& k, const std::string& v) {
  Stmt s(db_, "INSERT INTO kv(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
  s.text(1, k).text(2, v);
  s.step();
}

std::string Journal::kv_get(const std::string& k, const std::string& dflt) {
  Stmt s(db_, "SELECT value FROM kv WHERE key=?");
  s.text(1, k);
  return s.step() == SQLITE_ROW ? s.col_text(0) : dflt;
}

json run_journal_action(const json& in) {
  Journal j(get_str(in, "db"));
  j.init();
  const std::string a = get_str(in, "action");
  const int64_t now = static_cast<int64_t>(get_num(in, "now"));
  if (a == "init") return json{{"ok", true}};
  if (a == "propose") return j.propose(in.at("order"), now);
  if (a == "transition") return j.transition(get_str(in, "client_order_id"), get_str(in, "to"), now, in.value("detail", json::object()));
  if (a == "fill") return j.record_fill(get_str(in, "client_order_id"), get_num(in, "qty"), get_num(in, "price"), get_str(in, "fill_id"), now);
  if (a == "close_trade") return j.close_trade(in.at("trade"), now);
  if (a == "audit") { j.audit(get_str(in, "kind"), in.value("detail", json::object()), now); return json{{"ok", true}}; }
  if (a == "open_orders") return json{{"open_orders", j.open_orders(get_str(in, "mode"))}};
  if (a == "unknown_count") return json{{"unknown_orders", j.unknown_count(get_str(in, "mode"))}};
  if (a == "report") return j.report(get_str(in, "mode"), static_cast<int64_t>(get_num_or(in, "since", 0)));
  if (a == "kv_set") { j.kv_set(get_str(in, "key"), get_str(in, "value")); return json{{"ok", true}}; }
  if (a == "kv_get") return json{{"key", get_str(in, "key")}, {"value", j.kv_get(get_str(in, "key"), get_str_or(in, "default", ""))}};
  throw ValidationError("unknown journal action: " + a);
}

}  // namespace tca
