from argparse import ArgumentParser
from posixpath import exists
import shutil
from typing import Any
import json
from pathlib import Path

from datasets import Dataset, Features, Sequence, Image, Value, concatenate_datasets
from lerobot.common.datasets.video_utils import (
    decode_video_frames_torchvision,
    VideoFrame,
)
from lerobot.common.datasets.utils import (
    calculate_episode_data_index,
    hf_transform_to_torch,
)
from lerobot.common.datasets.compute_stats import compute_stats
from lerobot.common.datasets.lerobot_dataset import LeRobotDataset
from lerobot.common.datasets.utils import flatten_dict
import torch
from safetensors.torch import save_file


FEATURES = Features(
    {
        "observation.image": VideoFrame(),
        "observation.state": Sequence(
            length=7, feature=Value(dtype="float32", id=None)
        ),
        "action": Sequence(length=7, feature=Value(dtype="float32", id=None)),
        "episode_index": Value(dtype="int64", id=None),
        "frame_index": Value(dtype="int64", id=None),
        "timestamp": Value(dtype="float32", id=None),
        "index": Value(dtype="int64", id=None),
    }
)


def load_datadict(ep_idx: int, jsonfile: Path, videofile: Path):
    with open(jsonfile, "r") as fp:
        obj = json.load(fp)

    begin = obj["video_time_begin"]
    end = obj["video_time_end"]
    states = [s for s in obj["states"] if begin <= s["timestamp"] <= end]

    timestamps = [s["timestamp"] - begin for s in states]
    actions = [s["goal"] for s in states]
    obs = [s["position"] for s in states]

    num_frames = len(timestamps)
    assert num_frames == len(actions)
    assert num_frames == len(obs)

    # LeRobotDataset video variation will load frames during the __getitem__
    #images = decode_video_frames_torchvision(videofile, timestamps, 0.1)
    #images = [img.numpy().transpose(1, 2, 0) for img in (images * 255).to(torch.uint8)]

    # videofile should be copied to a relative path videos/
    return {
        "observation.image": [
            {"path": f"videos/{videofile.name}", "timestamp": t} for t in timestamps
        ],
        "observation.state": obs,
        "action": actions,
        "episode_index": [ep_idx] * num_frames,
        "frame_index": list(range(0, num_frames)),
        "timestamp": timestamps,
    }


def load_many(files) -> Dataset:
    dds = []
    index = 0
    for ep_idx, (jsonfile, videofile) in enumerate(files):
        datadict = load_datadict(ep_idx, jsonfile, videofile)
        num_frames = len(datadict["timestamp"])
        datadict["index"] = list(range(index, index + num_frames))
        index += num_frames
        dds.append(datadict)

    dss = [Dataset.from_dict(dd, features=FEATURES) for dd in dds]
    hf_dataset = concatenate_datasets(dss)
    hf_dataset.set_transform(hf_transform_to_torch)
    return hf_dataset


def to_lerobot_dataset(files, repo_id, root):
    hf_dataset = load_many(files)
    episode_data_index = calculate_episode_data_index(hf_dataset)
    info = {
        "video": 1,
        "fps": 10,
        "encoding": {
            "vcodec": "libsvtav1",
            "pix_fmt": "yuv420p",
            "g": 2,
            "crf": 30,
        },
    }

    lerobot_dataset = LeRobotDataset.from_preloaded(
        repo_id=repo_id,
        hf_dataset=hf_dataset,
        episode_data_index=episode_data_index,
        info=info,
        videos_dir=root / "videos",
    )
    return lerobot_dataset


def save_meta_data(
    info: dict[str, Any],
    stats: dict,
    episode_data_index: dict[str, list],
    meta_data_dir: Path,
):
    meta_data_dir.mkdir(parents=True, exist_ok=True)

    # save info
    info_path = meta_data_dir / "info.json"
    with open(str(info_path), "w") as f:
        json.dump(info, f, indent=4)

    # save stats
    stats_path = meta_data_dir / "stats.safetensors"
    save_file(flatten_dict(stats), stats_path)

    # save episode_data_index
    episode_data_index = {
        key: episode_data_index[key].clone().detach() for key in episode_data_index
    }
    ep_data_idx_path = meta_data_dir / "episode_data_index.safetensors"
    save_file(episode_data_index, ep_data_idx_path)


def save_video_files(files, local_dir):
    videos = local_dir / "videos"
    videos.mkdir(parents=True, exist_ok=True)
    for _, videofile in files:
        shutil.copy(videofile, videos)


def main():
    parser = ArgumentParser("Convert raw to LeRobotDataset")
    parser.add_argument("--raw-dir", type=str, default="output")
    parser.add_argument("--repo-id", type=str, required=True)
    parser.add_argument("raw", nargs="+")
    args = parser.parse_args()

    user_id, dataset_id = args.repo_id.split("/")
    local_dir = Path(f"datasets/{user_id}/{dataset_id}")

    raw_dir = Path(args.raw_dir)
    files = [(raw_dir / f"{r}.json", raw_dir / f"{r}.mp4") for r in args.raw]
    lds = to_lerobot_dataset(files, args.repo_id, local_dir)

    hf_dataset = lds.hf_dataset.with_format(
        None
    )  # to remove transforms that cant be saved
    hf_dataset.save_to_disk(str(local_dir / "train"))
    save_video_files(files, local_dir)

    stats = compute_stats(lds, batch_size=32, num_workers=8)
    save_meta_data(lds.info, stats, lds.episode_data_index, local_dir / "meta_data")


if __name__ == "__main__":
    main()
