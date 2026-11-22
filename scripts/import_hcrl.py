"""Import the supplied HCRL archive and prepare the assessment excerpts."""

import argparse
from itertools import islice
from pathlib import Path
from shutil import copyfileobj
from tempfile import TemporaryDirectory
from zipfile import ZipFile


def import_dataset(archive_path: Path, destination: Path) -> None:
    captures = (
        "normal_run_data/normal_run_data.txt",
        "Fuzzy_dataset.csv",
        "DoS_dataset.csv",
        "RPM_dataset.csv",
        "gear_dataset.csv",
    )
    excerpts = (
        ("Fuzzy_dataset.csv", "validation.csv"),
        ("DoS_dataset.csv", "benchmark-test.csv"),
    )
    destination.mkdir(parents=True, exist_ok=True)
    # Prepare everything before replacing an existing dataset.
    with ZipFile(archive_path) as archive, TemporaryDirectory(dir=destination) as temporary:
        staging = Path(temporary)
        for member in captures:
            with archive.open(member) as source, (staging / Path(member).name).open("wb") as output:
                copyfileobj(source, output)
        for source_name, excerpt_name in excerpts:
            with (staging / source_name).open("rb") as source:
                with (staging / excerpt_name).open("wb") as output:
                    output.writelines(islice(source, 200_000))
        for capture in staging.iterdir():
            capture.replace(destination / capture.name)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    dataset_dir = Path(__file__).resolve().parents[1] / "data" / "hcrl"
    import_dataset(args.archive, dataset_dir)
    print(f"Imported HCRL captures and excerpts into {dataset_dir}")
