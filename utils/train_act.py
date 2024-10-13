from pathlib import Path
import json

import torch
import neptune
from diffusers.optimization import get_scheduler

from lerobot.common.datasets.lerobot_dataset import LeRobotDataset
from lerobot.common.policies.act.configuration_act import ACTConfig
from lerobot.common.policies.act.modeling_act import ACTPolicy
from lerobot.common.datasets.transforms import get_image_transforms
from torch._functorch.apis import grad


run = neptune.init_run(project="jiasen/lerobot")

# Create a directory to store the training checkpoint.
output_directory = Path(f"outputs/train/parol6_pickup/{run._sys_id}")
output_directory.mkdir(parents=True, exist_ok=True)

device = torch.device("cuda")
log_freq = 50

training_steps = 8000

lr = 1.0e-4
lr_backbone = 1e-5

#lr = 1e-4
#lr_backbone = 1e-5

lr_scheduler = "cosine"

lr_warmup_steps = 500
adam_betas = [0.95, 0.999]
adam_eps = 1.0e-8
adam_weight_decay = 1.0e-6
batch_size = 16
grad_clip_norm = 10
chunk_size = 20
n_action_steps = 20


dataset_repo_id = "lerobot/parol6_pickup_0410"
FPS = 2

# to build up the batch["action"], must be num of chunk_size
# the key frames are in 1/FPS interval
delta_timestamps = {
    "action": [i / FPS for i in range(chunk_size)],
}


run["parameteres"] = {
    "dataset_repo_id": dataset_repo_id,
    "training_steps": training_steps,
    "delta_timestamps": json.dumps(delta_timestamps),
    "batch_size": batch_size,
    "lr": lr,
    "lr_backbone": lr_backbone,
    "lr_scheduler": lr_scheduler,
    "lr_warmup_steps": lr_warmup_steps,
    "grad_clip_norm": grad_clip_norm,
    "adam_betas": json.dumps(adam_betas),
    "adam_eps": adam_eps,
    "adam_weight_decay": adam_weight_decay,
    "chunk_size": chunk_size,
    "n_action_steps": n_action_steps,
}


transforms = get_image_transforms(
    brightness_weight=1,
    brightness_min_max=(0.8, 1.2),
    contrast_weight=1,
    contrast_min_max=(0.8, 1.2),
    saturation_weight=1,
    saturation_min_max=(0.5, 1.5),
    hue_weight=1,
    hue_min_max=(-0.05, 0.05),
    sharpness_weight=1,
    sharpness_min_max=(0.8, 1.2),
    max_num_transforms=3,
    random_order=False,
)


dataset = LeRobotDataset(
    dataset_repo_id,
    image_transforms=transforms,
    delta_timestamps=delta_timestamps,
    root="datasets",
)

dataset.stats["observation.images.top"]["mean"] = torch.tensor(
    [[[0.485]], [[0.456]], [[0.406]]], dtype=torch.float32
)
dataset.stats["observation.images.top"]["std"] = torch.tensor(
    [[[0.229]], [[0.224]], [[0.225]]], dtype=torch.float32
)
dataset.stats["observation.state"]["min"] = torch.tensor(
    [-6.42, -30, -41.9, -12.56, -8.7, -31.41, 0], dtype=torch.float32
)
dataset.stats["observation.state"]["max"] = torch.tensor(
    [21.84, 20, 41.9, 6, 8.7, 31.41, 40], dtype=torch.float32
)
dataset.stats["action"]["min"] = torch.tensor(
    [-6.42, -30, -41.9, -12.56, -8.7, -31.41, 0], dtype=torch.float32
)
dataset.stats["action"]["max"] = torch.tensor(
    [21.84, 20, 41.9, 6, 8.7, 31.41, 40], dtype=torch.float32
)

run["parameteres"]["dataset"] = str(dataset)
print(dataset)

cfg = ACTConfig(
    chunk_size=chunk_size,
    n_action_steps=n_action_steps,
    input_shapes={
        "observation.images.top": [3, 480, 640],
        "observation.state": [7],
    },
    output_shapes={
        "action": [7],
    },
    vision_backbone="resnet18",
    pretrained_backbone_weights="ResNet18_Weights.IMAGENET1K_V1",
    replace_final_stride_with_dilation=False,
    pre_norm=False,
    dim_model=512,
    n_heads=8,
    dim_feedforward=3200,
    feedforward_activation="relu",
    n_encoder_layers=4,
    n_decoder_layers=1,
    use_vae=True,
    latent_dim=32,
    n_vae_encoder_layers=4,
    dropout=0.1,
    kl_weight=10.0,

    input_normalization_modes={
        "observation.images.top": "mean_std",
        "observation.state": "min_max",
    },
    output_normalization_modes={
        "action": "min_max",
    },
)

policy = ACTPolicy(cfg, dataset_stats=dataset.stats)
policy.train()
policy.to(device)


optimizer_params_dicts = [
    {
        "params": [
            p
            for n, p in policy.named_parameters()
            if not n.startswith("model.backbone") and p.requires_grad
        ]
    },
    {
        "params": [
            p
            for n, p in policy.named_parameters()
            if n.startswith("model.backbone") and p.requires_grad
        ],
        "lr": lr_backbone,
    },
]

optimizer = torch.optim.Adam(
    optimizer_params_dicts,
    lr,
    adam_betas,
    adam_eps,
    adam_weight_decay,
)

if lr_scheduler is not None:
    lr_scheduler = get_scheduler(
        lr_scheduler,
        optimizer=optimizer,
        num_warmup_steps=lr_warmup_steps,
        num_training_steps=training_steps,
    )

# Create dataloader for offline training.
dataloader = torch.utils.data.DataLoader(
    dataset,
    num_workers=8,
    batch_size=batch_size,
    shuffle=True,
    pin_memory=device != torch.device("cpu"),
    drop_last=True,
)

# Run training loop.
step = 0
done = False
while not done:
    for batch in dataloader:
        batch = {k: v.to(device, non_blocking=True) for k, v in batch.items()}

        output_dict = policy.forward(batch)
        loss = output_dict["loss"]
        loss.backward()

        grad_norm = torch.nn.utils.clip_grad_norm_(
            policy.parameters(),
            grad_clip_norm,
            error_if_nonfinite=False,
        )
        optimizer.step()
        optimizer.zero_grad()
        if lr_scheduler is not None:
            lr_scheduler.step()

        for k, v in output_dict.items():
            if torch.is_tensor(v):
                v = v.item()
            run[f"training/{k}"].append(v)
        run["training/lr"].append(optimizer.param_groups[0]["lr"])
        run["training/grad_norm "].append(float(grad_norm))

        if step % log_freq == 0:
            print(f"step: {step} loss: {loss.item():.3f}")
        step += 1
        if step >= training_steps:
            done = True
            break

# Save a policy checkpoint.
policy.save_pretrained(output_directory)

# run["model/weights"].upload_files(output_directory)
run.stop()
