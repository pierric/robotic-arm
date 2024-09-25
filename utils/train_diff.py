from pathlib import Path
import json

import torch
import neptune
from diffusers.optimization import get_scheduler

from lerobot.common.datasets.lerobot_dataset import LeRobotDataset
from lerobot.common.policies.diffusion.configuration_diffusion import DiffusionConfig
from lerobot.common.policies.diffusion.modeling_diffusion import DiffusionPolicy


run = neptune.init_run(project="jiasen/lerobot")

# Create a directory to store the training checkpoint.
output_directory = Path(f"outputs/train/parol6_pickup/{run._sys_id}")
output_directory.mkdir(parents=True, exist_ok=True)

# Number of offline training steps (we'll only do offline training for this example.)
# Adjust as you prefer. 5000 steps are needed to get something worth evaluating.
training_steps = 5000
device = torch.device("cuda")
log_freq = 5

# Set up the dataset.
delta_timestamps = {
    # Load the previous image and state at -0.1 seconds before current frame,
    # then load current image and state corresponding to 0.0 second.
    "observation.images.top": [-0.25, 0.0],
    "observation.state": [-0.25, 0.0],
    # Load the previous action (-0.1), the next action to be executed (0.0),
    # and 14 future actions with a 0.1 seconds spacing. All these actions will be
    # used to supervise the policy.
    "action": [-0.25, 0.0, 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2, 2.25, 2.5, 2.75, 3, 3.25, 3.5],
}


lr = 1.0e-4
lr_scheduler = "cosine"
lr_warmup_steps = 500
adam_betas = [0.95, 0.999]
adam_eps = 1.0e-8
adam_weight_decay = 1.0e-6
horizon = 16
batch_size = 4
grad_clip_norm = 10

run["parameteres"] = {
    "training_steps": training_steps,
    "delta_timestamps": json.dumps(delta_timestamps),
    "horizon": horizon,
    "batch_size": batch_size,
    "lr": lr,
    "lr_scheduler": lr_scheduler,
    "lr_warmup_steps": lr_warmup_steps,
    "adam_betas": json.dumps(adam_betas),
    "adam_eps": adam_eps,
    "adam_weight_decay": adam_weight_decay,
    "grad_clip_norm": 10,
}


dataset = LeRobotDataset("lerobot/parol6_pickup", delta_timestamps=delta_timestamps, root="datasets")

# Set up the the policy.
# Policies are initialized with a configuration class, in this case `DiffusionConfig`.
# For this example, no arguments need to be passed because the defaults are set up for PushT.
# If you're doing something different, you will likely need to change at least some of the defaults.
cfg = DiffusionConfig(
    input_shapes = {
        "observation.images.top": [3, 480, 640],
        "observation.state": [7],
    },
    output_shapes = {
        "action": [7],
    },
    horizon = horizon,
    crop_shape = None,
    input_normalization_modes = {
      "observation.images.top": "mean_std",
      "observation.state": "min_max",
    }
)

policy = DiffusionPolicy(cfg, dataset_stats=dataset.stats)
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
