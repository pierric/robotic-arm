# Extract a path
```bash
python extract_path.py --name <path-name-in-mongo>
```
A json file will be saved in the `paths` folder.

# Extract the video and robot states
```bash
python extract_dataset.py --from-path paths/<path-name> <begin-timestamp> <end-timestamp>
```
The mp4 and json files are saved in the `output` folder, with the same name (a short uuid). The uuid is also printed at the end of the script.

# Convert to LeRebotDataset
```bash
python to_lerobot_dataset.py --repo-id lerobot/<dataset-name> <uuid1> <uuid2> ... 
```

The uuid1, uuid2, ... are the files that are extracted with the previous script. At the end, a LeRebotDataset is created in the `datasets` folder. Here is an example

```
datasets/
└── lerobot
    └── parol6_pickup
        ├── meta_data
        │   ├── episode_data_index.safetensors
        │   ├── info.json
        │   └── stats.safetensors
        ├── train
        │   ├── data-00000-of-00001.arrow
        │   ├── dataset_info.json
        │   └── state.json
        └── videos
            ├── Fp74bW4zqHwYYGMdSqNoi2.mp4
            ├── iddex93VwnDQHraDBfGGPp.mp4
            └── o7DCm5oPL4g4JfmFD5iZxe.mp4
```


# Visualize the dataset
```bash
python <lerobot_root>/lerobot/scripts/visualize_dataset_html.py --root=datasets --repo-id lerobot/<dataset-name>
```
