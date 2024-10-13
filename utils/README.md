# Extract a path
```bash
python extract_path.py --name <path-name-in-mongo>
```
A json file will be saved in the `paths` folder.

# Execute a path
```bash
python execute_path.py <path-file-name>
```
will run the every step in the path file. Start the camera recording at the 2nd step. Stop the camera after the last step.

# Extract the video and robot states
**NOTE**, it is important to adjust the two variable `FRAMERATE` and `DELAY` in the script to match the charastistic of the camera. For my AI thinker esp32 camera, taking VGA, they are 10 and 0.4 respectively, and for my M5 camera, they are 20 and 0.6 respectively.
```bash
python extract_dataset.py --from-path paths/<path-name> <begin-timestamp> <end-timestamp>
```
The mp4 and json files are saved in the `output` folder, with the same name (a short uuid). The uuid is also printed at the end of the script.

# Execute a path and extract the dataset
There is a script for convenience that execute and extract at once.
```bash
./execute_and_extract.sh <path-name>
```
Note that the argument is not the file path to the json file. It is only the filename. The script assumes the path file is stored in the paths folder, and will look for the `paths/<path-name>.json`.

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
