#!/usr/bin/env python3
"""Generate the fictional sample catalog used by the UI gallery and prototypes.

Development tool only. Every name, cover and lyric line is invented for Cirrus;
nothing is taken from real releases. Output is deterministic:
  ui/samples/covers/*.jpg
  ui/samples/catalog.json
  ui/samples/lyrics/*.json   (line text below; word timings are synthesized)
Requires Pillow.
"""

import json
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "ui" / "samples"
SIZE = 640


def lerp(a, b, t):
    return tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))


def vertical_gradient(top, bottom):
    image = Image.new("RGB", (SIZE, SIZE))
    draw = ImageDraw.Draw(image)
    for y in range(SIZE):
        draw.line([(0, y), (SIZE, y)], fill=lerp(top, bottom, y / (SIZE - 1)))
    return image


def grain(image, rng, amount):
    noise = Image.frombytes("L", (SIZE, SIZE), rng.randbytes(SIZE * SIZE)).convert("RGB")
    return Image.blend(image, noise, amount / 1000)


def sunset(rng):
    image = vertical_gradient((255, 140, 66), (122, 36, 120))
    draw = ImageDraw.Draw(image)
    draw.ellipse([180, 260, 460, 540], fill=(255, 214, 102))
    for i in range(6):
        y = 420 + i * 30
        draw.rectangle([0, y, SIZE, y + 12], fill=lerp((122, 36, 120), (60, 20, 70), i / 5))
    return grain(image, rng, 40)


def ocean(rng):
    image = vertical_gradient((10, 40, 90), (0, 150, 160))
    draw = ImageDraw.Draw(image)
    for k in range(9):
        points = [(x, 120 + k * 60 + 22 * math.sin(x / 55 + k)) for x in range(0, SIZE + 8, 8)]
        draw.line(points, fill=lerp((120, 220, 230), (20, 80, 140), k / 8), width=10)
    return grain(image, rng, 30)


def forest(rng):
    image = vertical_gradient((196, 210, 170), (44, 74, 52))
    draw = ImageDraw.Draw(image)
    for i in range(14):
        x = rng.randint(-40, SIZE)
        h = rng.randint(220, 460)
        shade = lerp((70, 110, 70), (24, 48, 34), rng.random())
        draw.polygon([(x, SIZE), (x + 60, SIZE - h), (x + 120, SIZE)], fill=shade)
    return grain(image, rng, 50).filter(ImageFilter.GaussianBlur(1))


def neon(rng):
    image = Image.new("RGB", (SIZE, SIZE), (12, 8, 24))
    draw = ImageDraw.Draw(image)
    for i in range(12):
        color = (255, 46, 196) if i % 2 else (40, 230, 255)
        offset = i * 52 - 120
        draw.line([(offset, SIZE), (offset + 360, 0)], fill=color, width=6)
    glow = image.filter(ImageFilter.GaussianBlur(14))
    return Image.blend(glow, image, 0.55)


def mono(rng):
    image = vertical_gradient((210, 210, 210), (40, 40, 40))
    draw = ImageDraw.Draw(image)
    for r in range(300, 20, -40):
        g = 255 - r // 2
        draw.ellipse([320 - r, 320 - r, 320 + r, 320 + r], outline=(g, g, g), width=6)
    return grain(image, rng, 60).convert("L").convert("RGB")


def pastel(rng):
    image = vertical_gradient((255, 214, 224), (198, 240, 222))
    draw = ImageDraw.Draw(image)
    for _ in range(18):
        r = rng.randint(30, 110)
        x, y = rng.randint(0, SIZE), rng.randint(0, SIZE)
        color = rng.choice([(255, 236, 179), (186, 222, 255), (224, 200, 255), (255, 255, 255)])
        draw.ellipse([x - r, y - r, x + r, y + r], fill=color)
    return image.filter(ImageFilter.GaussianBlur(18))


def crimson(rng):
    image = Image.new("RGB", (SIZE, SIZE), (196, 20, 36))
    draw = ImageDraw.Draw(image)
    draw.rectangle([140, 140, 500, 500], outline=(255, 245, 240), width=28)
    draw.line([(140, 500), (500, 140)], fill=(255, 245, 240), width=28)
    return grain(image, rng, 35)


def hazard(rng):
    image = Image.new("RGB", (SIZE, SIZE), (250, 204, 21))
    draw = ImageDraw.Draw(image)
    for i in range(-SIZE, SIZE * 2, 90):
        draw.polygon([(i, 0), (i + 45, 0), (i + 45 - SIZE, SIZE), (i - SIZE, SIZE)], fill=(20, 20, 20))
    draw.rectangle([120, 250, 520, 390], fill=(250, 204, 21))
    return image


def earth(rng):
    image = vertical_gradient((214, 180, 140), (92, 58, 36))
    draw = ImageDraw.Draw(image)
    for k in range(7):
        y = 200 + k * 55
        points = [(x, y + 18 * math.sin(x / 90 + k * 0.7)) for x in range(0, SIZE + 10, 10)]
        draw.line(points, fill=lerp((166, 120, 80), (70, 40, 24), k / 6), width=24)
    return grain(image, rng, 70)


def minimal(rng):
    image = Image.new("RGB", (SIZE, SIZE), (246, 244, 240))
    draw = ImageDraw.Draw(image)
    draw.ellipse([420, 140, 500, 220], fill=(36, 84, 200))
    draw.line([(140, 470), (500, 470)], fill=(60, 60, 60), width=4)
    return image


def galaxy(rng):
    image = vertical_gradient((24, 8, 48), (70, 20, 110))
    draw = ImageDraw.Draw(image)
    for _ in range(420):
        x, y = rng.randint(0, SIZE), rng.randint(0, SIZE)
        b = rng.randint(140, 255)
        s = rng.choice([1, 1, 1, 2, 3])
        draw.ellipse([x, y, x + s, y + s], fill=(b, b, min(255, b + 30)))
    cloud = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
    ImageDraw.Draw(cloud).ellipse([120, 180, 560, 460], fill=(200, 60, 160))
    return Image.blend(image, cloud.filter(ImageFilter.GaussianBlur(70)), 0.35)


def stripes(rng):
    image = Image.new("RGB", (SIZE, SIZE))
    draw = ImageDraw.Draw(image)
    colors = [(239, 68, 68), (249, 115, 22), (234, 179, 8), (34, 197, 94), (59, 130, 246), (139, 92, 246)]
    width = SIZE // len(colors)
    for i, color in enumerate(colors):
        draw.rectangle([i * width, 0, (i + 1) * width, SIZE], fill=color)
    return image.rotate(18, resample=Image.BICUBIC, expand=False, fillcolor=(30, 30, 30))


COVERS = [
    ("sunset", sunset),
    ("ocean", ocean),
    ("forest", forest),
    ("neon", neon),
    ("mono", mono),
    ("pastel", pastel),
    ("crimson", crimson),
    ("hazard", hazard),
    ("earth", earth),
    ("minimal", minimal),
    ("galaxy", galaxy),
    ("stripes", stripes),
]

ARTISTS = [
    ("ar-01", "雾港乐队"),
    ("ar-02", "Lumen Avenue"),
    ("ar-03", "林间信号"),
    ("ar-04", "Night Transit"),
    ("ar-05", "沈澄"),
    ("ar-06", "Pastel Orbit"),
    ("ar-07", "赤色回声"),
    ("ar-08", "Kiiro"),
    ("ar-09", "土与木"),
    ("ar-10", "Quiet Margin"),
    ("ar-11", "星屑合唱团"),
    ("ar-12", "Prism Theory"),
]

# album id, title, artist ids, cover (None = missing), year, song titles
ALBUMS = [
    ("al-01", "落日航线", ["ar-01"], "sunset", 2024, ["落日航线", "海风写给你的信", "Harbor Lights", "慢慢靠岸"]),
    ("al-02", "Tidal Memory", ["ar-02"], "ocean", 2023, ["Tidal Memory", "Undertow", "Blue Hour (Reprise)"]),
    ("al-03", "树冠之下", ["ar-03"], "forest", 2022, ["树冠之下", "苔藓", "清晨六点的森林广播"]),
    ("al-04", "Last Train Home", ["ar-04", "ar-12"], "neon", 2025, ["Last Train Home", "Signal / Noise", "Midnight Platform 3"]),
    ("al-05", "灰度", ["ar-05"], "mono", 2021, ["灰度", "黑白电影院", "没有颜色的那一年"]),
    ("al-06", "Cotton Candy Cosmos", ["ar-06"], "pastel", 2024, ["Cotton Candy Cosmos", "Soft Landing"]),
    ("al-07", "回声", ["ar-07"], "crimson", 2020, ["回声", "红色警报", "燃烧的城市"]),
    ("al-08", "注意 / Caution", ["ar-08"], "hazard", 2025, ["注意 / Caution", "きいろい夢", "Yellow Line"]),
    ("al-09", "泥土的温度", ["ar-09"], "earth", 2019, ["泥土的温度", "陶罐", "秋收"]),
    ("al-10", "Margin", ["ar-10"], "minimal", 2023, ["Margin", "Empty Room", "Footnote"]),
    ("al-11", "星屑", ["ar-11"], "galaxy", 2022, ["星屑", "银河铁道的最后一班车", "Nebula"]),
    ("al-12", "Prism", ["ar-12"], "stripes", 2024, ["Prism", "Spectrum Shift", "White Light"]),
    ("al-13", "未发行的小样合集（封面缺失、标题很长用于测试截断和换行效果的专辑）", ["ar-05", "ar-01"], None, 2026, ["Demo #1", "无题"]),
]

PLAYLISTS = [
    ("pl-01", "深夜通勤", "sample-user", "neon", "下班路上的电子与城市流行。", ["al-04", "al-02", "al-11"]),
    ("pl-02", "周末慢下来", "sample-user", "pastel", "适合发呆、整理房间和午后小睡。", ["al-06", "al-10", "al-03"]),
    ("pl-03", "华语独立精选", "Cirrus 示例", "sunset", None, ["al-01", "al-03", "al-05", "al-09"]),
    ("pl-04", "Workout Mix", "Cirrus 示例", "crimson", "High energy, no skips.", ["al-07", "al-08", "al-12"]),
    ("pl-05", "空歌单", "sample-user", None, None, []),
]

# Invented lyrics: (start ms, original, translation, romanization). Empty text = instrumental gap.
LYRIC_LINES = {
    "so-001": [
        (0, "", None, None),
        (12000, "把落日折成一张船票", "Folding the sunset into a ferry ticket", "bǎ luò rì zhé chéng yì zhāng chuán piào"),
        (17500, "塞进你外套的口袋", "Slipped into the pocket of your coat", "sāi jìn nǐ wài tào de kǒu dài"),
        (23000, "海面上的灯一盏一盏亮起来", "Lights on the sea come on one by one", "hǎi miàn shàng de dēng yì zhǎn yì zhǎn liàng qǐ lái"),
        (29500, "像是有人在远方替我们说晚安", "As if someone far away were saying goodnight for us", "xiàng shì yǒu rén zài yuǎn fāng tì wǒ men shuō wǎn ān"),
        (37000, "", None, None),
        (44000, "落日航线 没有终点", "The sunset route has no final stop", "luò rì háng xiàn méi yǒu zhōng diǎn"),
        (49000, "只有风在耳边", "Only the wind beside our ears", "zhǐ yǒu fēng zài ěr biān"),
        (53500, "落日航线 慢一点", "Sunset route, go a little slower", "luò rì háng xiàn màn yì diǎn"),
        (58500, "让这一刻再长一点点", "Let this moment last a little longer", "ràng zhè yí kè zài cháng yì diǎn diǎn"),
        (65000, "这是一句故意写得很长很长的歌词，用来测试歌词视图在窄窗口下的自动换行、行间距和逐字高亮是否正常", "A deliberately long line to test wrapping, spacing and word highlighting in narrow windows", None),
        (75000, "慢慢靠岸", "Slowly coming ashore", "màn màn kào àn"),
    ],
    "so-005": [
        (0, "", "", None),
        (9000, "The tide keeps every word we left behind", "潮水留住了我们留下的每一句话", None),
        (14500, "Folded in the sand like paper boats", "像纸船一样折叠在沙里", None),
        (20000, "I count the waves and lose the count again", "我数着浪花，又一次数乱了", None),
        (26000, "Blue hour, hold on", "蓝调时刻，请再停留", None),
        (30500, "", "", None),
        (38000, "Tidal memory, pulling me home", "潮汐的记忆，把我拉回家", None),
        (43500, "Every echo knows your name", "每一声回响都知道你的名字", None),
        (49000, "Tidal memory, never alone", "潮汐的记忆，从不孤单", None),
        (54500, "Undertow and afterglow", "暗流与余晖", None),
    ],
    # Japanese, for the romaji ruby (docs/ui/pages/player.md §6.3): particles, yōon,
    # sokuon, long vowels, readings in brackets, a backing vocal in brackets, English,
    # and a last line whose romanization misses a word (it must fall back).
    "so-031": [
        (0, "", None, None),
        (10000, "夜空に星屑が降る", "星屑从夜空落下", "yozora ni hoshikuzu ga furu"),
        (15500, "君は今日も遠くを見てる", "你今天也望着远方", "kimi wa kyou mo tooku wo miteru"),
        (21000, "ちょっと待って もう一回", "等一下 再来一次", "chotto matte mou ikkai"),
        (27000, "運命(さだめ)なんて信じない", "我才不相信什么命运", "sadame nante shinjinai"),
        (33000, "(oh yeah) 光のスピードで", "（oh yeah）以光的速度", "oh yeah hikari no supiido de"),
        (39000, "", None, None),
        (46000, "本気(マジ)で好きだよ", "是真的喜欢你啊", "maji de suki da yo"),
        (51500, "星屑のメロディー", "星屑的旋律", "hoshikuzu no merodii"),
        (57000, "ずっとずっと歌っていたい", "想一直一直唱下去", "zutto zutto utatte itai"),
        (64000, "東京へ行こう", "去东京吧", "toukyou e ikou"),
        (69000, "さよならは言わないで", "请不要说再见", "sayonara wa iwanai"),
    ],
}

# Per line (by start ms): "hold" gives one word 40% of the sung time (a long note);
# "romajiWords" times the romanization by syllable, as (text, original words spanned).
LYRIC_EXTRAS = {
    "so-031": {
        10000: {"romajiWords": [("yo", 1), ("zora", 1), ("ni", 1), ("hoshi", 1), ("kuzu", 1),
                                ("ga", 1), ("fu", 1), ("ru", 1)]},
        51500: {"romajiWords": [("hoshi", 1), ("kuzu", 1), ("no", 1), ("me", 1), ("ro", 1),
                                ("di", 2), ("i", 1)]},
        57000: {"hold": 11},
    },
}

LYRICS = {song_id: f"lyrics/{song_id}.json" for song_id in LYRIC_LINES}


def build_lyrics(song_id, duration_ms):
    lines = LYRIC_LINES[song_id]
    result = []
    for index, (start, text, translation, romanization) in enumerate(lines):
        end = lines[index + 1][0] if index + 1 < len(lines) else min(duration_ms, start + 8000)
        cjk = any("\u3040" <= c <= "\u30ff" or "\u4e00" <= c <= "\u9fff" for c in text)
        tokens = list(text) if cjk else text.split(" ")
        tokens = [t for t in tokens if t.strip()]
        extras = LYRIC_EXTRAS.get(song_id, {}).get(start, {})
        sung = max(1, round((end - start) * 0.85))
        # Word boundaries as fractions of the sung time; a held word takes 40%.
        weights = [1.0] * len(tokens)
        if "hold" in extras and tokens:
            weights[extras["hold"]] = 0.4 / 0.6 * (len(tokens) - 1)
        bounds = [0.0]
        for w in weights:
            bounds.append(bounds[-1] + w)
        times = [start + round(sung * b / bounds[-1]) for b in bounds] if tokens else []
        words = [{"startMs": times[i], "endMs": times[i + 1], "text": token}
                 for i, token in enumerate(tokens)]
        line = {
            "startMs": start,
            "endMs": end,
            "text": text,
            "translation": translation or None,
            "romanization": romanization,
            "words": words,
        }
        if "romajiWords" in extras:
            romaji, first = [], 0
            for syllable, span in extras["romajiWords"]:
                romaji.append({"startMs": times[first], "endMs": times[first + span],
                               "text": syllable})
                first += span
            assert first == len(tokens), (song_id, start)
            line["romanizationWords"] = romaji
        result.append(line)
    return {
        "songId": song_id,
        "note": "Invented sample lyrics; word timings are synthesized.",
        "lines": result,
    }


def build_catalog(rng):
    songs, albums = [], []
    counter = 0
    for album_id, title, artist_ids, cover, year, titles in ALBUMS:
        song_ids = []
        for index, song_title in enumerate(titles):
            counter += 1
            song_id = f"so-{counter:03d}"
            song = {
                "id": song_id,
                "title": song_title,
                "artistIds": artist_ids,
                "albumId": album_id,
                "trackNumber": index + 1,
                "durationMs": rng.randint(150, 330) * 1000,
                "playable": True,
            }
            songs.append(song)
            song_ids.append(song_id)
        albums.append({
            "id": album_id,
            "title": title,
            "artistIds": artist_ids,
            "cover": f"covers/{cover}.jpg" if cover else None,
            "year": year,
            "songIds": song_ids,
        })

    by_id = {s["id"]: s for s in songs}
    by_id["so-008"]["durationMs"] = None  # unknown duration must not render as 00:00
    by_id["so-012"]["playable"] = False
    by_id["so-012"]["unavailableReason"] = "示例：该歌曲在当前地区不可播放"
    for song_id in LYRICS:
        by_id[song_id]["lyrics"] = LYRICS[song_id]

    album_songs = {a["id"]: a["songIds"] for a in albums}
    playlists = [{
        "id": pid,
        "title": title,
        "creator": creator,
        "cover": f"covers/{cover}.jpg" if cover else None,
        "description": description,
        "songIds": [sid for aid in source_albums for sid in album_songs[aid]],
    } for pid, title, creator, cover, description, source_albums in PLAYLISTS]

    return {
        "note": "Fictional sample data generated by tools/ui/generate_samples.py. Not real releases.",
        "artists": [{"id": a, "name": n} for a, n in ARTISTS],
        "albums": albums,
        "songs": songs,
        "playlists": playlists,
    }


def write_samples_qml(covers):
    entries = ",\n".join(f'        "{name}"' for name in covers)
    (OUT / "Samples.qml").write_text(f"""// Generated by tools/ui/generate_samples.py. Do not edit.
pragma Singleton
import QtQml

// Entry point to the fictional sample catalog bundled for the gallery and prototypes.
QtObject {{
    readonly property url catalog: Qt.resolvedUrl("catalog.json")
    readonly property list<string> covers: [
{entries}
    ]

    function cover(name: string): url {{
        return Qt.resolvedUrl("covers/" + name + ".jpg");
    }}

    function lyrics(songId: string): url {{
        return Qt.resolvedUrl("lyrics/" + songId + ".json");
    }}
}}
""", encoding="utf-8")


def main():
    rng = random.Random(20261017)
    (OUT / "covers").mkdir(parents=True, exist_ok=True)
    for name, painter in COVERS:
        painter(random.Random(name)).save(OUT / "covers" / f"{name}.jpg", quality=88, optimize=True)
    catalog = build_catalog(rng)
    (OUT / "lyrics").mkdir(exist_ok=True)
    durations = {s["id"]: s["durationMs"] for s in catalog["songs"]}
    for song_id, path in LYRICS.items():
        (OUT / path).write_text(
            json.dumps(build_lyrics(song_id, durations[song_id]), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8")
    (OUT / "catalog.json").write_text(
        json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    write_samples_qml([name for name, _ in COVERS])
    print(f"{len(COVERS)} covers, {len(catalog['songs'])} songs, {len(catalog['playlists'])} playlists")


if __name__ == "__main__":
    main()
