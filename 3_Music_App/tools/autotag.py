"""Fill in missing / garbled song details on the console from MusicBrainz.

Run on a PC on the same WiFi while the console's "Add songs" screen is open:

    python autotag.py                 # look songs up, write a report, change nothing
    python autotag.py --apply         # ...and send the confident matches to the console
    python autotag.py --undo          # put back what the last --apply changed

For each song that has a garbled (??), missing artist or missing album, it
searches MusicBrainz (https://musicbrainz.org, free and open; one lookup a
second, as they ask) using the song's tags or its original file name - file
names on the card keep their real Japanese / Korean / Hindi text even when
the console's screen can't draw it. A match is only used when MusicBrainz is
sure AND the title and artist really resemble the file. The album picked is
the original release where possible (not a compilation or live album).
Names in other scripts are written in Latin letters (the console's font has
nothing else): Japanese via pykakasi, Chinese via pypinyin, the rest via
unidecode. Those three are optional (pip install pykakasi pypinyin unidecode).

Edits go through the organizer's /api/edit, so they are stored like hand
edits (/.jiggly/edits.txt on the card) and the song files are untouched.
"""
import argparse
import difflib
import json
import os
import re
import sys
import time
import unicodedata
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
UA = "JigglypuffOrganizer/1.0 ( https://github.com/tanmayJbadhan/ANO-Ipod )"
MB = "https://musicbrainz.org/ws/2/recording"

try:
    import pykakasi
    _kks = pykakasi.kakasi()
except ImportError:
    _kks = None
try:
    from pypinyin import lazy_pinyin
except ImportError:
    lazy_pinyin = None
try:
    from unidecode import unidecode
except ImportError:
    unidecode = None


# --- text helpers ----------------------------------------------------------

def has(pattern, s):
    return re.search(pattern, s) is not None

KANA = r"[぀-ヿ]"
HAN = r"[一-鿿]"
HANGUL = r"[가-힯ᄀ-ᇿ]"


def romanize(s, japanese=False):
    """Latin-letter version of s for the console's screen. Kanji alone can't
    tell Japanese from Chinese, so `japanese` says what the song is."""
    if not s or all(ord(c) < 0x250 for c in s):
        return s
    out = s
    if (has(KANA, s) or japanese) and _kks:
        out = " ".join(w["hepburn"] for w in _kks.convert(s) if w["hepburn"].strip())
    elif has(HAN, s) and lazy_pinyin:  # Chinese
        out = " ".join(lazy_pinyin(s))
    if unidecode:
        out = unidecode(out)
    out = re.sub(r"\s+", " ", out).strip()
    return out[:1].upper() + out[1:] if out else s


def norm(s):
    """For comparing: same width, case, no punctuation."""
    s = unicodedata.normalize("NFKC", s or "").casefold()
    s = re.sub(r"[\W_]+", " ", s)
    return re.sub(r"\s+", " ", s).strip()


def sim(a, b):
    a, b = norm(a), norm(b)
    if not a or not b:
        return 0.0
    if a in b or b in a:
        return max(0.85, difflib.SequenceMatcher(None, a, b).ratio())
    return difflib.SequenceMatcher(None, a, b).ratio()


JUNK = re.compile(
    r"[\(\[\{][^\)\]\}]*(official|video|audio|lyric|mv|m/v|hd|hq|4k|full song|visualizer|"
    r"remaster|explicit|clean|color coded|eng|sub|live|demo|bonus|\d{4})[^\)\]\}]*[\)\]\}]",
    re.I)


def clean_stem(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    stem = re.sub(r"\[[^\]]*\]", " ", stem)  # "[MP3FY]", "[Full]"
    stem = JUNK.sub(" ", stem)
    stem = re.sub(r"[\(\[]?[^()\[\]]*(color.?coded|lyrics?|han.rom|rom.eng|가사)[^()\[\]]*[\)\]]?", " ", stem, flags=re.I)
    stem = re.sub(r"\s-\s*by\s+\S+.*$", " ", stem, flags=re.I)  # " - by YankaT"
    stem = re.sub(r"^\s*\d{1,3}\s*(?:[-._)]\s*|\s+)", "", stem)  # "01 - ", "01. ", "03.Title", "02 Title"
    stem = re.sub(r"\s+[_|–—]\s+", " - ", stem)  # "Title _ Artist", "A | B", "A – B"
    stem = re.sub(r"[_]+", " ", stem)
    stem = re.sub(r"\b(official|music video|video|audio|full song|hd)\b", " ", stem, flags=re.I)
    stem = re.sub(r"\(\s*\d+\s*\)", " ", stem)  # "(1)" copies
    return re.sub(r"\s+", " ", stem).strip(" -")


def screen_title(s, japanese=False):
    """Title for the console: if it's in another script but carries an
    English name in brackets - "나비효과 (Butterfly Effect)" - use that."""
    if s and any(ord(c) >= 0x250 for c in s):
        m = re.search(r"[\(\[]([\x20-\x7e]*[A-Za-z][\x20-\x7e]*)[\)\]]", s)
        if m and not re.search(r"feat|ver|remix|inst", m.group(1), re.I):
            return m.group(1).strip()
    return romanize(s, japanese)


def is_japanese(path, rec):
    """Kana anywhere about the song, or a release from Japan."""
    text = path + rec.get("title", "") + json.dumps(rec.get("artist-credit", []), ensure_ascii=False)
    if has(KANA, text):
        return True
    return any(r.get("country") == "JP" or has(KANA, r.get("title", "")) for r in rec.get("releases", []))


def lucene(s):
    return re.sub(r'([+\-&|!(){}\[\]^"~*?:\\/])', r"\\\1", s)


# --- MusicBrainz -------------------------------------------------------------

_last = [0.0]


def mb_search(query):
    wait = 1.1 - (time.time() - _last[0])
    if wait > 0:
        time.sleep(wait)
    url = MB + "?" + urllib.parse.urlencode({"query": query, "fmt": "json", "limit": 8})
    for attempt in range(4):
        _last[0] = time.time()
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.load(r).get("recordings", [])
        except urllib.error.HTTPError as e:
            if e.code in (429, 503):
                time.sleep(3 + attempt * 3)
                continue
            return []
        except Exception:
            time.sleep(2)
    return []


def credit_name(rec, latin=False, japanese=False):
    parts = []
    for c in rec.get("artist-credit", []):
        name = c.get("name") or c.get("artist", {}).get("name", "")
        if latin and any(ord(ch) >= 0x250 for ch in name):
            sort = c.get("artist", {}).get("sort-name", "")
            if sort and all(ord(ch) < 0x250 for ch in sort):
                # "Nakamori, Akina" -> "Akina Nakamori"
                name = " ".join(reversed([p.strip() for p in sort.split(",", 1)])) if "," in sort else sort
            else:
                name = romanize(name, japanese)
        parts.append(name + c.get("joinphrase", ""))
    return "".join(parts).strip()


def best_release(rec):
    best, best_score = None, -99
    for r in rec.get("releases", []):
        rg = r.get("release-group", {})
        score = 0
        if r.get("status") == "Official":
            score += 2
        score += {"Album": 3, "EP": 2, "Single": 1}.get(rg.get("primary-type"), 0)
        sec = rg.get("secondary-types", [])
        if "Compilation" in sec:
            score -= 4
        if "Live" in sec or "Remix" in sec or "Soundtrack" in sec:
            score -= 5
        if re.search(r"\b(live|concert|dvd|tour|best|collection|greatest)\b", r.get("title", ""), re.I):
            score -= 4
        if any(c.get("name") == "Various Artists" for c in r.get("artist-credit", [])):
            score -= 4
        date = r.get("date", "") or "9999"
        key = (score, -int(date[:4]) if date[:4].isdigit() else -9999)
        if best is None or key > best_score:
            best, best_score = r, key
    return best


# Folders named after a genre or a disc say nothing about artist or album.
GENERIC = {"kpop", "jpop", "jrock", "cpop", "pop", "rock", "alternative", "soft tunes", "asian retro",
           "ru", "music", "songs", "misc", "various", "new folder", "downloads", "mp3", "cd"}


def clean_folder(name):
    # Drop site tags and bitrates - "[www.k2nblog.com]", "(320kbps)" - but
    # keep other bracketed text: "1st Album [Dystopia The Tree of Language]".
    name = re.sub(r"[\[\(][^\]\)]*(www|\.com|\.net|kbps|mp3)[^\]\)]*[\]\)]", " ", name, flags=re.I)
    name = re.sub(r"[\[\]]", " ", name)
    name = re.sub(r"\b(mp3|flac|320kbps)\b", " ", name, flags=re.I)
    name = name.replace("–", "-").replace("—", "-").replace("_", " ")
    return re.sub(r"\s+", " ", name).strip(" -")


def folder_hints(path):
    """(artist, album, sure) guesses from the song's folders. sure = the
    folder spelled out "Artist - Album", so its album name is trusted."""
    parts = [p for p in os.path.dirname(path).split("/") if p]
    parts = [clean_folder(p) for p in parts if not re.fullmatch(r"(cd|disc)\s*\d+", p.strip(), re.I)]
    parts = [re.sub(r"^\d{4}-\d{2}-\d{2}\s+", "", p) for p in parts]  # "2017-11-01 ..."
    parts = [p for p in parts if p and norm(p) not in GENERIC]
    if not parts:
        return None, None, False
    last = parts[-1]
    for sep in (" - ", "-"):
        if sep in last:
            a, b = [x.strip() for x in last.split(sep, 1)]
            if a and b:
                return a, b, True
    if len(parts) >= 2:
        return parts[-2], last, True  # /Artist/Album/
    return last, last, False  # one name: could be either - tried both ways


def bare(s):
    """Title without bracketed extras ("(feat. X)", "[Remastered]")."""
    return norm(re.sub(r"[\(\[][^\)\]]*[\)\]]", " ", s or ""))


def title_sim(a, b):
    a, b = bare(a), bare(b)
    if not a or not b:
        return 0.0
    return difflib.SequenceMatcher(None, a, b).ratio()


def candidates(song):
    """(title, artist or None, album or None) guesses to look up, best first."""
    title, artist, album, path = song[0], song[1], song[2], song[3]
    stem = clean_stem(path)
    f_artist, f_album, _ = folder_hints(path)
    if f_artist and norm(f_artist) in norm(stem) and norm(stem) != norm(f_artist):
        # "Mind Reader Lexie Liu" in folder "Lexie Liu" -> "Mind Reader"
        stem = re.sub(re.escape(f_artist), " ", stem, flags=re.I).strip(" -")
    ok = lambda v, unknown: v and "??" not in v and v != unknown
    out = []
    if ok(title, U["title"]):
        out.append((title, artist if ok(artist, U["artist"]) else f_artist,
                    album if ok(album, U["album"]) and norm(album) not in GENERIC else f_album))
    if " - " in stem:
        a, b = [x.strip() for x in stem.split(" - ", 1)]
        if a and b:
            out.append((b, a, f_album))
            out.append((a, b, f_album))
    if stem:
        out.append((stem, f_artist, f_album))
    seen, uniq = set(), []
    for c in out:
        if c not in seen:
            seen.add(c)
            uniq.append(c)
    return uniq[:3]


def queries(song):
    """Lucene queries for each guess: by artist, and by album when the album
    is a separate hint (or the folder might be either)."""
    def q(s):  # search without bracketed extras: "EXO (엑소)" -> "EXO"
        stripped = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", s or "").strip()
        return stripped or s

    out = []
    for t, a, al in candidates(song):
        t, a, al = q(t), q(a) if a else a, q(al) if al else al
        if a:
            out.append((f'recording:"{lucene(t)}" AND artist:"{lucene(a)}"', t, a, al))
        if al and (not a or al == a):
            out.append((f'recording:"{lucene(t)}" AND release:"{lucene(al)}"', t, None, al))
        if not a and not al:
            out.append((lucene(t), t, None, None))
    return out[:4]


def matching_release(rec, album):
    if not album:
        return None
    best, best_s = None, 0.0
    for r in rec.get("releases", []):
        s = title_sim(r.get("title", ""), album)
        if s > best_s:
            best, best_s = r, s
    return best if best_s >= 0.7 else None


def identify(song):
    stem = clean_stem(song[3])
    if re.search(r"\bcover", stem, re.I):
        return None  # a cover isn't the original recording's artist
    f_artist, f_album, f_sure = folder_hints(song[3])
    for query, want_title, want_artist, want_album in queries(song):
        for rec in mb_search(query):
            mb_score = int(rec.get("score", 0))
            t_sim = title_sim(rec.get("title", ""), want_title)
            if mb_score < 80 or t_sim < 0.8:
                continue
            artist = credit_name(rec)
            rel_hint = matching_release(rec, want_album)
            a_ok = bool(want_artist) and sim(artist, want_artist) >= 0.6
            if not want_artist and not want_album:
                # Bare file name: the artist has to be in it too.
                a_ok = bool(norm(artist)) and norm(artist) in norm(stem)
            if not (a_ok or rel_hint):
                continue
            rel = rel_hint or best_release(rec)
            jp = is_japanese(song[3], rec)
            album = screen_title(rel["title"], jp) if rel else ""
            if f_sure and f_album and not rel_hint:
                album = screen_title(f_album, jp)  # the folder says which album this copy is from
            return {
                "title": screen_title(rec.get("title", ""), jp),
                "artist": credit_name(rec, latin=True, japanese=jp),
                "album": album,
                "confidence": round(min(mb_score / 100, t_sim), 2),
                "mbid": rec.get("id"),
                "query": query,
            }
    return None


# --- second pass (--fill): songs MusicBrainz couldn't confirm ---------------
# 1. the song's own tags, read in full from the file (the console only keeps
#    what its font can draw, so a Japanese title tag shows there as ????),
# 2. the iTunes catalogue (better K-pop / J-pop / Bollywood coverage; the US
#    store gives English names where they exist),
# 3. a best guess from the file and folder names.
# Only fields that are missing or garbled get filled.

ITUNES = "https://itunes.apple.com/search"
_last_it = [0.0]


def itunes_search(term):
    wait = 3.1 - (time.time() - _last_it[0])  # Apple allows ~20 a minute
    if wait > 0:
        time.sleep(wait)
    url = ITUNES + "?" + urllib.parse.urlencode(
        {"term": term[:100], "media": "music", "entity": "song", "limit": 10, "country": "US"})
    for attempt in range(3):
        _last_it[0] = time.time()
        try:
            req = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(req, timeout=20) as r:
                return json.load(r).get("results", [])
        except urllib.error.HTTPError as e:
            if e.code in (403, 429, 503):
                time.sleep(20 + attempt * 20)
                continue
            return []
        except Exception:
            time.sleep(3)
    return []


def raw_tags(base, sid):
    """Title / artist / album straight from the file's tags, in their own
    script. Reads only the start of the file (where MP3 / FLAC tags and
    most M4A tags live)."""
    try:
        import io
        import mutagen
        req = urllib.request.Request(f"{base}/api/song?id={sid}", headers={"Range": "bytes=0-262143"})
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        f = mutagen.File(io.BytesIO(data), easy=True)
        if not f or not f.tags:
            return {}
        get = lambda k: (f.tags.get(k) or [""])[0].strip()
        return {"title": get("title"), "artist": get("albumartist") or get("artist"), "album": get("album")}
    except Exception:
        return {}


def good(v, unknown):
    return bool(v) and "??" not in v and v != unknown


def itunes_terms(song, tags):
    """Up to three short searches, most specific first."""
    stem = clean_stem(song[3])
    f_artist, _, _ = folder_hints(song[3])
    a_hint = tags.get("artist") or (song[1] if good(song[1], U["artist"]) else None) or f_artist
    t_hint = tags.get("title")
    if not t_hint and " - " in stem:
        a, b = [x.strip() for x in stem.split(" - ", 1)]
        t_hint, a_hint = (b, a_hint or a)
    t_hint = t_hint or stem
    terms = []
    if a_hint and norm(a_hint) not in norm(t_hint):
        terms.append(f"{a_hint} {t_hint}")
    terms.append(" ".join(t_hint.split()[:6]))
    latin = " ".join(re.findall(r"[A-Za-z][A-Za-z'&.]*(?:\s+[A-Za-z0-9][A-Za-z0-9'&.]*)*", stem))
    if latin and norm(latin) != norm(terms[-1]) and len(latin) >= 6:
        terms.append(" ".join(latin.split()[:6]))
    out = []
    for t in terms:
        if t and t not in out:
            out.append(t)
    return out[:3], t_hint, a_hint


def tight(s):
    return norm(s).replace(" ", "")


def identify_itunes(song, tags):
    stem = clean_stem(song[3])
    terms, t_hint, a_hint = itunes_terms(song, tags)
    where = tight(song[3]) + tight(stem)
    for term in terms:
        for r in itunes_search(term):
            title, artist = r.get("trackName", ""), r.get("artistName", "")
            jp = has(KANA, title + artist)
            names = {title, romanize(title, jp)}
            t_best = max(title_sim(n, t_hint) for n in names)
            # The title may be either side of "A - B", spaced differently, or in
            # another script than the file name ("taiyou ga..." vs 太陽が...).
            t_ok = t_best >= 0.75 or any(len(tight(bare(n))) >= 4 and tight(bare(n)) in where for n in names)
            a_names = {artist, romanize(artist, jp)}
            a_ok = (any(a_hint and sim(n, a_hint) >= 0.6 for n in a_names)
                    or any(len(tight(n)) >= 2 and tight(n) in where for n in a_names))
            if not a_ok and t_best >= 0.92 and len(bare(t_hint)) >= 12:
                a_ok = True  # long, near-exact title: the artist can't be checked but it's this song
            if t_ok and a_ok and not re.search(r"\bcover|karaoke|tribute|instrumental", title + r.get("collectionName", ""), re.I):
                return {"title": screen_title(title), "artist": screen_title(artist),
                        "album": screen_title(r.get("collectionName", "")), "source": "itunes", "query": term}
    return None


def guess(song, tags):
    """Readable names from the song's own tags or its file / folder names."""
    jp = has(KANA, song[3] + "".join(tags.values()))
    stem = clean_stem(song[3])
    f_artist, f_album, f_sure = folder_hints(song[3])
    title, artist = tags.get("title"), tags.get("artist")
    if not title:
        title = stem
        if " - " in stem:
            a, b = [x.strip() for x in stem.split(" - ", 1)]
            if f_artist and sim(b, f_artist) > sim(a, f_artist):
                a, b = b, a
            title, artist = b, artist or a
        elif f_artist and norm(f_artist) in norm(stem) and norm(stem) != norm(f_artist):
            title = re.sub(re.escape(f_artist), " ", stem, flags=re.I).strip(" -")
        # Mixed-script file names usually carry an English title: prefer it
        # to romanizing everything ("Showa Genroku Rakugo Shinjuu Op 1").
        latin = re.findall(r"[A-Za-z][A-Za-z0-9'&.]*", title)
        if any(ord(c) >= 0x250 for c in title) and len(latin) >= 3:
            title = " ".join(re.findall(r"[A-Za-z0-9][A-Za-z0-9'&.]*(?:\s+[A-Za-z0-9][A-Za-z0-9'&.]*)*", title))
    if not artist and f_sure:
        artist = f_artist
    album = tags.get("album") or f_album
    out = {"source": "tags" if tags.get("title") else "filename"}
    for k, v in (("title", title), ("artist", artist), ("album", album)):
        if v:
            out[k] = screen_title(v, jp)
    return out


def fill(base, report, songs):
    """Adds a 'fill' to every report entry MusicBrainz didn't match."""
    todo = [r for r in report if not r["match"] and not r.get("fill")]
    print(f"Second pass over {len(todo)} songs (own tags, iTunes, then file names; about {len(todo) * 7 // 60 + 1} min)…")
    for n, r in enumerate(todo, 1):
        s = songs[r["id"]]
        tags = raw_tags(base, r["id"]) if "??" in (s[0] + s[1] + s[2]) or s[1] == U["artist"] else {}
        tags = {k: v for k, v in tags.items() if v}
        m = identify_itunes(s, tags) or guess(s, tags)
        # Fill what's missing or garbled (a genre folder like "alternative"
        # standing in for the album counts as missing). A confirmed iTunes
        # match also replaces the title, which is usually the raw file name.
        cur = {"title": (s[0], U["title"]), "artist": (s[1], U["artist"]), "album": (s[2], U["album"])}
        missing = {k for k, (v, unk) in cur.items() if not good(v, unk)}
        if norm(s[2]) in GENERIC:
            missing.add("album")
        if m["source"] == "itunes":
            missing.add("title")
        r["fill"] = {k: v for k, v in m.items() if k in missing and v} | {"source": m["source"]}
        if n % 10 == 0 or n == len(todo):
            src = {}
            for x in report:
                if x.get("fill"):
                    src[x["fill"]["source"]] = src.get(x["fill"]["source"], 0) + 1
            print(f"  {n}/{len(todo)}  {src}", flush=True)
    return report


# --- console -----------------------------------------------------------------

def http(base, path, data=None, timeout=60):
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    req = urllib.request.Request(base + path, data=body)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.load(r)


def needs_fix(s):
    return ("??" in (s[0] + s[1] + s[2]) or s[1] == U["artist"] or s[2] == U["album"]
            or s[0] == U["title"])


U = {}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--console", default="http://jigglypuff.local", help="console address")
    ap.add_argument("--apply", action="store_true", help="send confident matches to the console")
    ap.add_argument("--undo", action="store_true", help="revert the last --apply")
    ap.add_argument("--all", action="store_true", help="also look up songs that already look fine")
    ap.add_argument("--fill", action="store_true",
                    help="second pass over the last report's unmatched songs: own tags, iTunes, file names")
    ap.add_argument("--limit", type=int, default=0, help="only the first N songs (testing)")
    ap.add_argument("--sample", type=int, default=0, help="a random N of the songs (testing)")
    ap.add_argument("--out", default=os.path.join(HERE, "autotag-report.json"))
    args = ap.parse_args()
    base = args.console.rstrip("/")
    undo_path = os.path.join(HERE, "autotag-undo.json")

    if args.undo:
        old = json.load(open(undo_path, encoding="utf-8"))
        for sid, (t, a, al) in old.items():
            http(base, "/api/edit", {"ids": sid, "title": t, "artist": a, "album": al})
        print(f"Put back {len(old)} songs.")
        return

    print("Reading the console's library…")
    lib = http(base, "/api/library", timeout=180)
    U.update(lib["unknown"])
    songs = lib["songs"]

    if args.fill:
        report = json.load(open(args.out, encoding="utf-8"))
        fill(base, report, songs)
        json.dump(report, open(args.out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        if not args.apply:
            print("Nothing changed. Run again with --fill --apply to send these to the console.")
            return
        undo = json.load(open(undo_path, encoding="utf-8")) if os.path.exists(undo_path) else {}
        sent = 0
        for r in report:
            f = r.get("fill") or {}
            data = {k: f[k] for k in ("title", "artist", "album") if f.get(k)}
            if not data or r.get("filled"):
                continue
            undo.setdefault(str(r["id"]), songs[r["id"]][:3])
            http(base, "/api/edit", {"ids": r["id"], **data})
            r["filled"] = True
            sent += 1
        json.dump(undo, open(undo_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        json.dump(report, open(args.out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        print(f"Filled in {sent} more songs on the console.")
        return
    todo = [i for i, s in enumerate(songs) if args.all or needs_fix(s)]
    if args.sample:
        import random
        todo = sorted(random.Random(1).sample(todo, min(args.sample, len(todo))))
    if args.limit:
        todo = todo[: args.limit]
    print(f"{len(songs)} songs, looking up {len(todo)} (about {len(todo) * 2 // 60 + 1} min)…")

    report, found = [], 0
    for n, i in enumerate(todo, 1):
        s = songs[i]
        m = identify(s)
        if m:
            found += 1
        report.append({"id": i, "path": s[3], "was": s[:3], "match": m})
        if n % 10 == 0 or n == len(todo):
            print(f"  {n}/{len(todo)}  matched {found}", flush=True)
            json.dump(report, open(args.out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

    print(f"Matched {found} of {len(todo)}. Report: {args.out}")
    if not args.apply:
        print("Nothing changed. Run again with --apply to send the matches to the console.")
        return

    undo = {}
    for r in report:
        m = r["match"]
        if not m:
            continue
        undo[str(r["id"])] = r["was"]
        http(base, "/api/edit", {"ids": r["id"], "title": m["title"], "artist": m["artist"], "album": m["album"]})
    json.dump(undo, open(undo_path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"Updated {len(undo)} songs on the console. (python autotag.py --undo puts them back)")


if __name__ == "__main__":
    main()
