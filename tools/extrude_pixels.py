import argparse
from typing import List
from pathlib import Path
from PIL import Image
import numpy as np


def _parse_args():
    args = argparse.ArgumentParser()
    args.add_argument("--inp-dir", type=str)
    args.add_argument("--out-dir", type=str)

    return args.parse_args()

def _get_image_file_paths(dir: Path) -> List[Path]:
    file_paths = []
    for file_path in dir.iterdir():
        if file_path.suffix == ".png":
            file_paths.append(file_path)
    
    return file_paths



def main():
    args = _parse_args()
    inp_dir = Path(args.inp_dir)
    out_dir = Path(args.out_dir)
    assert inp_dir != out_dir
    out_dir.mkdir(exist_ok=True, parents=True)

    inp_file_paths = _get_image_file_paths(inp_dir)

    for i_file_path, file_path in enumerate(inp_file_paths):
        image = Image.open(file_path)
        inp_pixels = np.array(image)
        out_pixels = np.copy(inp_pixels)
        out_file_path = out_dir / file_path.name

        if inp_pixels.shape[2] == 3:
            image.save(out_file_path)
            continue

        n_rows, n_cols, *_ = inp_pixels.shape

        new_color = np.zeros((4, ), dtype=np.float32)
        steps = ((-1, 0), (0, -1), (1, 0), (0, 1))
        for i in range(n_rows):
            for j in range(n_cols):
                a = inp_pixels[i, j, 3]
                if a == 0:
                    new_color *= 0
                    n_new_colors = 0
                    for sx, sy in steps:
                        si = i + sy
                        sj = j + sx
                        if si < 0 or si >= n_rows:
                            continue
                        if sj < 0 or sj >= n_cols:
                            continue
                        if inp_pixels[si, sj, 3] > 0:
                            new_color += inp_pixels[si, sj, :]
                            n_new_colors += 1

                    if n_new_colors == 0:
                        continue
                    
                    new_color /= n_new_colors

                    out_pixels[i, j, :] = new_color.astype(np.uint8)

        new_image = Image.fromarray(out_pixels)
        new_image.save(out_file_path)
        print(f"{i_file_path}/{len(inp_file_paths)}")


if __name__ == "__main__":
    main()
