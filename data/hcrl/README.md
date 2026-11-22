# HCRL vehicle captures

This directory holds the local import of `9) Car-Hacking Dataset.zip`.

| File | Role |
| --- | --- |
| `normal_run_data.txt` | Full normal recording for training |
| `Fuzzy_dataset.csv` | Source for the validation excerpt |
| `DoS_dataset.csv` | Full held-out DoS test and source for the performance benchmark |
| `RPM_dataset.csv` | Full held-out RPM spoofing test |
| `gear_dataset.csv` | Full held-out gear spoofing test |
| `validation.csv` | First 200,000 Fuzzy records |
| `benchmark-test.csv` | First 200,000 DoS records |

The large capture files are stored locally and ignored by Git. See the project README for the import command, assessment commands, and results. [HCRL dataset and citation](https://ocslab.hksecurity.net/Datasets/car-hacking-dataset/)
