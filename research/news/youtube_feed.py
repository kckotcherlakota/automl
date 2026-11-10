"""List recent videos of a YouTube channel from its public RSS feed (no API key, no transcripts).
Usage: python3 -I research/news/youtube_feed.py [channel_id] [hours=30]
Default channel: Verified Investing (@verifiedinvesting) UCZ-J2m1AUSLnifUEKam5_dA.
Output is untrusted third-party commentary: context only, never a trade signal (V3 §12).
"""
import sys, urllib.request, xml.etree.ElementTree as E
from datetime import datetime, timezone, timedelta
cid = sys.argv[1] if len(sys.argv) > 1 else "UCZ-J2m1AUSLnifUEKam5_dA"
hours = float(sys.argv[2]) if len(sys.argv) > 2 else 30
ns = {"a": "http://www.w3.org/2005/Atom", "m": "http://search.yahoo.com/mrss/", "yt": "http://www.youtube.com/xml/schemas/2015"}
xml = urllib.request.urlopen(urllib.request.Request(
    f"https://www.youtube.com/feeds/videos.xml?channel_id={cid}", headers={"User-Agent": "Mozilla/5.0"}), timeout=30).read()
cut = datetime.now(timezone.utc) - timedelta(hours=hours)
for e in E.fromstring(xml).findall("a:entry", ns):
    pub = datetime.fromisoformat(e.find("a:published", ns).text)
    if pub < cut:
        continue
    et = pub.astimezone(timezone(timedelta(hours=-5))).strftime("%Y-%m-%d %H:%M ET")
    vid = e.find("yt:videoId", ns).text
    desc = (e.find("m:group/m:description", ns).text or "").replace("\n", " ")
    print(f"- {et} | {e.find('a:title', ns).text} | https://youtu.be/{vid}\n    {desc[:300]}")
