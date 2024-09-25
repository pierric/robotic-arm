from pathlib import Path
import json

import torch
import neptune
from diffusers.optimization import get_scheduler

from lerobot.common.datasets.lerobot_dataset import LeRobotDataset
from lerobot.common.policies.act.configuration_act import ACTConfig
from lerobot.common.policies.act.modeling_act import ACTPolicy


run = neptune.init_run(project="jiasen/lerobot")

# Create a directory to store the training checkpoint.
output_directory = Path(f"outputs/train/parol6_pickup/{run._sys_id}")
output_directory.mkdir(parents=True, exist_ok=True)

device = torch.device("cuda")
log_freq = 5

training_steps = 7000
lr = 1.0e-4
lr_scheduler = "cosine"
lr_warmup_steps = 500
adam_betas = [0.95, 0.999]
adam_eps = 1.0e-8
adam_weight_decay = 1.0e-6
batch_size = 16
grad_clip_norm = 10
chunk_size = 50
n_action_steps = 50

# to build up the batch["action"], must be num of chunk_size
# the key frames are in 0.25s interval
delta_timestamps = {
    "action": [i * 0.25 for i in range(chunk_size)],
}


run["parameteres"] = {
    "training_steps": training_steps,
    "delta_timestamps": json.dumps(delta_timestamps),
    "batch_size": batch_size,
    "lr": lr,
    "lr_scheduler": lr_scheduler,
    "lr_warmup_steps": lr_warmup_steps,
    "grad_clip_norm": 10,
    "adam_betas": json.dumps(adam_betas),
    "adam_eps": adam_eps,
    "adam_weight_decay": adam_weight_decay,
    "chunk_size": chunk_size,
    "n_action_steps": n_action_steps,
}


dataset = LeRobotDataset(
    "lerobot/parol6_pickup", delta_timestamps=delta_timestamps, root="datasets"
)

dataset.stats["observation.images.top"]["mean"] = torch.tensor(
    [[[0.485]], [[0.456]], [[0.406]]], dtype=torch.float32
)
dataset.stats["observation.images.top"]["std"] = torch.tensor(
    [[[0.229]], [[0.224]], [[0.225]]], dtype=torch.float32
)

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
)

policy = ACTPolicy(cfg, dataset_stats=dataset.stats)
policy.train()
policy.to(device)

optimizer = torch.optim.Adam(
    policy.parameters(),
    lr,
    adam_betas,
    adam_eps,
    adam_weight_decay,
)
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
        lr_scheduler.step()

        run["training/loss"].append(loss.item())
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
