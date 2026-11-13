"""Spike E: check that la-train's PyTorch really computes on a Cradle GPU (docs/spikes/E-python-gpu.md).

Run inside a GPU job with la-train's python, called by its full path (on this account `conda activate`
leaves another env's bin first on PATH, docs/cradle.md):
    srun -p <partition> --gres=gpu:1 -c 4 --mem=16G -t 10 ~/beanRant/envs/la-train/bin/python -s cluster/scripts/gpu_check.py
Exits 1 if any check fails.
"""
import os
import sys
import time

import numpy as np
import torch

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
failures = []


def check(name, ok, detail):
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: {detail}")
    if not ok:
        failures.append(name)


print(f"python {sys.version.split()[0]} ({sys.executable}) | torch {torch.__version__} | CUDA build {torch.version.cuda} | "
      f"cuDNN {torch.backends.cudnn.version()} | numpy {np.__version__}")
check("cuda available", torch.cuda.is_available(), f"device count {torch.cuda.device_count()}")
if not torch.cuda.is_available():
    sys.exit(1)
dev = torch.device("cuda:0")
props = torch.cuda.get_device_properties(dev)
print(f"device: {props.name} | sm_{props.major}{props.minor} | {props.total_memory / 2**30:.0f} GiB | "
      f"arch list in this build: {torch.cuda.get_arch_list()}")
torch.manual_seed(0)

# 1. Matrix multiply: GPU fp32 vs a CPU float64 reference. TF32 is off by default for matmul.
a, b = torch.randn(4096, 4096), torch.randn(4096, 4096)
ref = (a.double() @ b.double())
ga, gb = a.to(dev), b.to(dev)
torch.cuda.synchronize()
t0 = time.perf_counter()
for _ in range(10):
    gc = ga @ gb
torch.cuda.synchronize()
dt = (time.perf_counter() - t0) / 10
rel = ((gc.double().cpu() - ref).norm() / ref.norm()).item()
check("matmul 4096x4096 fp32", rel < 1e-5, f"relative error vs CPU float64 {rel:.2e}, {2 * 4096**3 / dt / 1e12:.1f} TFLOP/s")

# 2. GRU forward + backward through cuDNN (the policy has a GRU memory), same weights on CPU and GPU.
gru_cpu = torch.nn.GRU(input_size=32, hidden_size=64, batch_first=True)
gru_gpu = torch.nn.GRU(input_size=32, hidden_size=64, batch_first=True).to(dev)
gru_gpu.load_state_dict(gru_cpu.state_dict())
x = torch.randn(256, 16, 32)
out_cpu, _ = gru_cpu(x)
out_gpu, _ = gru_gpu(x.to(dev))
out_cpu.square().mean().backward()
out_gpu.square().mean().backward()
fwd = (out_gpu.cpu() - out_cpu).abs().max().item()
bwd = (gru_gpu.weight_hh_l0.grad.cpu() - gru_cpu.weight_hh_l0.grad).abs().max().item()
check("GRU cuDNN fwd+bwd", fwd < 1e-4 and bwd < 1e-4, f"max |GPU-CPU| output {fwd:.1e}, grad {bwd:.1e}")

# 3. A small MLP trained with Adam on the GPU (the trainer's networks are small MLPs): the loss must drop.
net = torch.nn.Sequential(torch.nn.Linear(8, 128), torch.nn.ELU(), torch.nn.Linear(128, 1)).to(dev)
opt = torch.optim.Adam(net.parameters(), lr=1e-3)
xs = torch.randn(4096, 8, device=dev)
ys = xs.sin().sum(dim=1, keepdim=True)
losses = []
for _ in range(300):
    loss = torch.nn.functional.mse_loss(net(xs), ys)
    opt.zero_grad()
    loss.backward()
    opt.step()
    losses.append(loss.item())
check("Adam training on GPU", losses[-1] < 0.2 * losses[0], f"loss {losses[0]:.3f} -> {losses[-1]:.3f} in 300 steps")

# 4. The trainer's own imports (smart_open, tensorboard, the socket communicator) load in this env.
sys.path.insert(0, os.path.join(REPO, "cluster", "learning_agents_python"))
try:
    from importlib.metadata import version  # smart_open reports 0.0.0 in the conda-forge build (conda says 7.3.0)
    import learning_agents.train_ppo  # noqa: F401  (imports smart_open at load)
    import learning_agents.communicators.socket_communicator  # noqa: F401
    check("trainer imports", True, f"smart_open {version('smart_open')}, tensorboard {version('tensorboard')}")
except Exception as e:  # report, don't crash: the other checks still count
    check("trainer imports", False, repr(e))

print(f"peak GPU memory {torch.cuda.max_memory_allocated() / 2**20:.0f} MiB")
print("RESULT:", "FAIL " + ", ".join(failures) if failures else "all checks passed")
sys.exit(1 if failures else 0)
