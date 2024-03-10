import json
import re
from collections import defaultdict
from dataclasses import dataclass
from itertools import chain
from pathlib import Path
from typing import Tuple

import cv2
import numpy as np

_THIS_DIR = Path(__file__).parent
_ROOT_DIR = _THIS_DIR / ".."
_ASEPRITE_DIR = _ROOT_DIR / "aseprite"
_ASSETS_DIR = _ROOT_DIR / "resources/sprite_sheets"

_SPRITE_LAYER = "sprite"
_MASK_LAYER_PREFIX = "mask_"


@dataclass
class Sprite:
    name: str
    frame_idx: int
    image: np.ndarray
    tl: Tuple[int, int]

    @property
    def h(self):
        return self.image.shape[0]

    @property
    def w(self):
        return self.image.shape[1]

    @property
    def tr(self):
        return (self.tl[0] + self.w, self.tl[1])

    @property
    def bl(self):
        return (self.tl[0], self.tl[1] + self.h)

    def get_neighbor_tls(self):
        bl = self.bl
        tr = self.tr
        return [(bl[0], bl[1] + 1), (tr[0] + 1, tr[1])]

    def to_meta(self):
        # NOTE:
        # When convertic sprite to the meta (coordinates on the sprite sheet)
        # we shrink its size by 1 pixel on each size, because the sprite
        # has been extruded and we need to un-extrude its size back when
        # saving the sheet meta
        x, y = self.tl
        x += 1
        y += 1

        w, h = self.w, self.h
        w -= 2
        h -= 2

        return {
            "name": self.name,
            "x": int(x),
            "y": int(y),
            "w": int(w),
            "h": int(h),
            "frame_idx": int(self.frame_idx),
        }


@dataclass
class Mask:
    name: str
    frame_idx: int
    tl: Tuple[int, int]
    w: int
    h: int

    def to_meta(self):
        x, y = self.tl

        return {
            "name": self.name,
            "x": int(x),
            "y": int(y),
            "w": int(self.w),
            "h": int(self.h),
            "frame_idx": int(self.frame_idx),
        }


if __name__ == "__main__":
    out_dir = _ASSETS_DIR
    out_dir.mkdir(exist_ok=True, parents=True)

    # --------------------------------------------------------------------
    # Parse aseprite files and extract sprite images from png images
    sprites = defaultdict(list)
    masks = defaultdict(lambda: defaultdict(defaultdict))
    for file_path in _ASEPRITE_DIR.iterdir():
        if not str(file_path).endswith(".json"):
            continue

        dir_ = file_path.parent
        sprite_name = re.sub(r"\.json$", "", file_path.name)
        meta_fp = dir_ / (sprite_name + ".json")
        with open(meta_fp) as f:
            meta = json.load(f)
        frames = meta["frames"]
        meta = meta["meta"]

        sheet_fp = dir_ / meta["image"]
        sheet = cv2.imread(str(sheet_fp), cv2.IMREAD_UNCHANGED)
        layer_names = [layer["name"] for layer in meta["layers"]]
        if _SPRITE_LAYER not in layer_names:
            raise ValueError(f"{meta_fp} is missing the `sprite` layer")

        for frame in frames:
            name = frame["filename"]
            sprite_name, layer_name, tag, frame_idx = name.split(".")
            if tag:
                sprite_name += f"_{tag}"
            frame_idx = int(frame_idx)

            is_mask = False
            if layer_name.startswith(_MASK_LAYER_PREFIX):
                layer_name = re.sub(r"^mask_", "", layer_name)
                is_mask = True
            elif layer_name != _SPRITE_LAYER:
                raise ValueError(
                    f"Layer name: {layer_name} is not valid. "
                    "The layer name should be `sprite` (which represents the actual sprite) "
                    "or start with `mask_`."
                )

            x, y, w, h = (frame["frame"][k] for k in ("x", "y", "w", "h"))

            if layer_name == _SPRITE_LAYER:
                # Expand the sprite frame on 1 pixel on both sides, because
                # the sheet contains extruded sprites
                x -= 1
                y -= 1
                w += 2
                h += 2

                image = sheet[y : y + h, x : x + w, :]
                sprite = Sprite(
                    name=sprite_name,
                    frame_idx=frame_idx,
                    image=image,
                    tl=(0, 0),
                )
                sprites[sprite_name].append(sprite)
            elif is_mask:
                image = sheet[y : y + h, x : x + w, :].max(-1)
                mask = None
                if image.max() > 0:
                    row = image.max(0)
                    col = image.max(1)
                    left_x = row.argmax()
                    top_y = col.argmax()
                    right_x = len(row) - row[::-1].argmax() - 1
                    bot_y = len(col) - col[::-1].argmax() - 1

                    # Shift the mask on 1 pixel because the corresponding
                    # sprite has been expanded
                    # left_x += 1
                    # right_x += 1
                    bot_y += 1
                    top_y += 1

                    w = right_x - left_x + 1
                    h = bot_y - top_y + 1

                    bot_left = (left_x, bot_y)
                    top_right = (right_x, top_y)
                    mask = Mask(
                        name=sprite_name,
                        frame_idx=frame_idx,
                        tl=(left_x, top_y),
                        w=w,
                        h=h,
                    )

                    masks[sprite_name][layer_name][frame_idx] = mask
            else:
                assert False, f"Unhandled layer: {layer_name}"

    # --------------------------------------------------------------------
    # Pack sprites on the sheet
    flat_sprites = list(chain(*sprites.values()))
    inds = sorted(
        range(len(flat_sprites)), key=lambda i: -flat_sprites[i].image.size
    )

    sheet = np.zeros((0, 0, 5), dtype=np.uint8)

    tls_to_try = [(0, 0)]

    for sprite in flat_sprites:
        best_tl_idx = None
        while best_tl_idx is None:
            for i, tl in enumerate(tls_to_try):
                min_x, min_y = tl
                max_x = min_x + sprite.w
                max_y = min_y + sprite.h
                if (
                    sheet.shape[1] > max_x
                    and sheet.shape[0] > max_y
                    and sheet[min_y:max_y, min_x:max_x, -1].max() == 0
                ):
                    best_tl_idx = i
                    break

            if best_tl_idx is None:
                new_sheet = np.zeros(
                    (
                        sheet.shape[0] + sprite.h,
                        sheet.shape[1] + sprite.w,
                        5,
                    ),
                    dtype=np.uint8,
                )
                new_sheet[: sheet.shape[0], : sheet.shape[1]] = sheet
                sheet = new_sheet

        tl = tls_to_try.pop(best_tl_idx)
        sprite.tl = tl

        min_x, min_y = tl
        max_x = min_x + sprite.w
        max_y = min_y + sprite.h
        sheet[min_y:max_y, min_x:max_x, :-1] = sprite.image
        sheet[min_y:max_y, min_x:max_x, -1] = 1
        tls_to_try.extend(sprite.get_neighbor_tls())

    sheet = sheet[..., :-1]
    # --------------------------------------------------------------------
    # Prepare sheet meta file (sprites and colliders coordinates)
    sheet_h, sheet_w = sheet.shape[:2]
    meta = {
        "size": [sheet_w, sheet_h],
        "frames": defaultdict(list),
    }
    for sprite_name in sprites:
        sprites_ = sprites[sprite_name]

        for i in range(len(sprites_)):
            sprite = sprites_[i]
            sprite_meta = sprite.to_meta()

            masks_meta = {}
            for mask_name in masks[sprite_name]:
                m = masks[sprite_name][mask_name].get(sprite.frame_idx)
                if m:
                    masks_meta[mask_name] = m.to_meta()

            frame_meta = {
                _SPRITE_LAYER: sprite_meta,
                "masks": masks_meta,
            }
            meta["frames"][sprite.name].append(frame_meta)

    # Save the final sheet and meta json
    cv2.imwrite(str(out_dir / "0.png"), sheet)
    with open(out_dir / "0.json", "w") as f:
        json.dump(meta, f, indent=4)
