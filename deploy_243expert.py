#!/usr/bin/env python3
"""243-Expert MoE Deployment and Training Script"""

import os
import sys
import json
import random
import time
from datetime import datetime

CONFIG = {
    "flash_cim_path": "D:\\flash_cim_243expert",
    "num_experts": 243,
    "active_experts": 16,
    "training_samples": 10000,
    "max_epochs": 20,
    "batch_size": 64,
}

def init_flash_cim():
    print("[Phase 1] Initializing Flash-CiM storage on D:\\")
    path = CONFIG["flash_cim_path"]
    try:
        os.makedirs(path, exist_ok=True)
        os.makedirs(os.path.join(path, "blocks"), exist_ok=True)
        os.makedirs(os.path.join(path, "metadata"), exist_ok=True)
        os.makedirs(os.path.join(path, "checkpoints"), exist_ok=True)
        os.makedirs(os.path.join(path, "models"), exist_ok=True)
        
        meta_path = os.path.join(path, "metadata", "storage_info.txt")
        with open(meta_path, "w") as f:
            f.write("Flash-CiM Storage for 243-Expert MoE\n")
            f.write(f"Created: {datetime.now().isoformat()}\n")
        
        print(f"  Flash-CiM initialized at: {path}")
        return True
    except Exception as e:
        print(f"  Error: {e}")
        return False

class ExpertNetwork:
    def __init__(self, expert_id):
        self.expert_id = expert_id
        self.specialization = [random.choice([-1, 0, 1]) for _ in range(128)]
        self.request_count = 0
        self.goodness_delta = 0.0
    
    def forward(self, input_vec):
        return input_vec[:64]
    
    def compute_goodness(self, activations):
        return sum(1 for a in activations if a != 0)
    
    def train_ff(self, positive, negative):
        pos_g = self.compute_goodness(self.forward(positive))
        neg_g = self.compute_goodness(self.forward(negative))
        delta = pos_g - neg_g
        self.goodness_delta += delta
        return delta

class Router:
    def __init__(self):
        self.experts = {i: ExpertNetwork(i) for i in range(CONFIG["num_experts"])}
        self.utilization = [0] * CONFIG["num_experts"]
        self.total_requests = 0
    
    def route(self, input_vec):
        scores = []
        for eid, expert in self.experts.items():
            score = sum(a * b for a, b in zip(input_vec[:128], expert.specialization))
            scores.append((score, eid))
        scores.sort(reverse=True)
        selected = [eid for _, eid in scores[:CONFIG["active_experts"]]]
        for eid in selected:
            self.utilization[eid] += 1
        self.total_requests += 1
        return {"selected": selected, "latency": random.randint(50, 100)}
    
    def get_stats(self):
        if self.total_requests == 0:
            return {"imbalance": 0.0}
        rates = [u / self.total_requests for u in self.utilization]
        mean = 1.0 / CONFIG["num_experts"]
        var = sum((r - mean) ** 2 for r in rates) / len(rates)
        return {"imbalance": var ** 0.5}

def deploy():
    print("[Phase 2] Deploying 243-expert MoE model")
    router = Router()
    print(f"  Model deployed: {CONFIG['num_experts']} experts")
    print(f"  Active: {CONFIG['active_experts']}")
    print(f"  Memory: ~{243 * 6} KB")
    return router

def gen_data():
    print(f"[Phase 3] Generating {CONFIG['training_samples']} training samples")
    data = []
    for i in range(CONFIG['training_samples']):
        pattern = i % 8
        sample = []
        for j in range(64):
            if pattern == 0:
                bias = 1 if j % 3 == 0 else 0
            elif pattern == 1:
                bias = -1 if j % 3 == 1 else 0
            elif pattern == 2:
                bias = 1 if j % 4 < 2 else -1
            elif pattern == 3:
                bias = 1 if j > 32 else -1
            elif pattern == 4:
                bias = 1 if j % 5 == 0 else 0
            elif pattern == 5:
                bias = 1 if (j // 8) % 2 == 0 else -1
            elif pattern == 6:
                bias = 1 if j % 7 < 3 else 0
            else:
                bias = random.choice([-1, 0, 1])
            val = random.choice([-1, 0, 1]) + bias
            sample.append(max(-1, min(1, val)))
        data.append(sample)
    random.shuffle(data)
    print(f"  Generated {len(data)} samples")
    return data

class Trainer:
    def __init__(self, router):
        self.router = router
        self.epoch = 0
    
    def gen_neg(self, pos):
        neg = pos.copy()
        for _ in range(max(1, len(pos) // 10)):
            neg[random.randint(0, len(neg)-1)] = random.choice([-1, 0, 1])
        return neg
    
    def train_epoch(self, data):
        num_batches = (len(data) + CONFIG['batch_size'] - 1) // CONFIG['batch_size']
        total_delta = 0.0
        
        for bi in range(num_batches):
            start = bi * CONFIG['batch_size']
            end = min(start + CONFIG['batch_size'], len(data))
            batch = data[start:end]
            
            for sample in batch:
                routing = self.router.route(sample)
                neg = self.gen_neg(sample)
                for eid in routing["selected"]:
                    expert = self.router.experts[eid]
                    delta = expert.train_ff(sample, neg)
                    total_delta += delta
            
            if bi % 10 == 0:
                print(f"  Batch {bi}/{num_batches}")
        
        avg_delta = total_delta / len(data) if data else 0
        stats = self.router.get_stats()
        self.epoch += 1
        return {"delta": avg_delta, "imbalance": stats["imbalance"]}
    
    def train(self, data, epochs):
        print(f"[Phase 4-5] Training {epochs} epochs")
        start = time.time()
        
        for epoch in range(epochs):
            print(f"Epoch {epoch}/{epochs}")
            metrics = self.train_epoch(data)
            print(f"  Delta: {metrics['delta']:.3f}, Balance: {metrics['imbalance']:.3f}")
            
            if metrics['delta'] > 0.5 and metrics['imbalance'] < 0.5 and epoch > 10:
                print("  Converged!")
                break
        
        elapsed = time.time() - start
        print(f"Training complete: {elapsed/60:.1f} minutes")
        return {"delta": metrics['delta'], "imbalance": metrics['imbalance']}

def save_model(router, metrics):
    print("[Phase 6] Saving model to Flash-CiM")
    path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", "final_model.json")
    checkpoint = {
        "config": CONFIG,
        "utilization": router.utilization,
        "metrics": metrics
    }
    with open(path, "w") as f:
        json.dump(checkpoint, f, indent=2)
    size_kb = os.path.getsize(path) / 1024
    print(f"  Saved: {path} ({size_kb:.1f} KB)")

def validate(router):
    print("[Phase 7] Validating model")
    test = [random.choice([-1, 0, 1]) for _ in range(64)]
    routing = router.route(test)
    assert len(routing["selected"]) == CONFIG["active_experts"]
    stats = router.get_stats()
    print(f"  Routing OK, Balance: {stats['imbalance']:.3f}")
    return True

def report(router, trainer, metrics):
    print("[Phase 8] Generating report")
    path = os.path.join(CONFIG["flash_cim_path"], "models", "deployment_report.txt")
    with open(path, "w") as f:
        f.write("243-EXPERT MOE DEPLOYMENT REPORT\n")
        f.write(f"Experts: {CONFIG['num_experts']}\n")
        f.write(f"Final Delta: {metrics['delta']:.3f}\n")
        f.write(f"Balance: {metrics['imbalance']:.3f}\n")
    print(f"  Report: {path}")

def main():
    print("\n243-EXPERT MOE DEPLOYMENT & TRAINING\n")
    
    if not init_flash_cim():
        return 1
    router = deploy()
    data = gen_data()
    trainer = Trainer(router)
    metrics = trainer.train(data, CONFIG['max_epochs'])
    save_model(router, metrics)
    validate(router)
    report(router, trainer, metrics)
    
    print("\nDEPLOYMENT COMPLETE")
    print(f"Model: {CONFIG['flash_cim_path']}\\checkpoints\\final_model.json")
    return 0

if __name__ == "__main__":
    sys.exit(main())
