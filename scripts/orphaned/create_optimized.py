import os

script = '''#!/usr/bin/env python3
import os, sys, json, random, time
from multiprocessing import Pool, Manager
from datetime import datetime

CONFIG = {
    "flash_cim_path": os.getenv("FLASH_CIM_PATH", "D:\\\\flash_cim_243expert_stress"),
    "num_experts": 243,
    "active_experts": 16,
    "training_samples": 500000,
    "max_epochs": 1000,
    "batch_size": 1024,
    "workers": 8,
}

class ExpertNetwork:
    def __init__(self, expert_id):
        self.expert_id = expert_id
        self.specialization = [random.choice([-1, 0, 1]) for _ in range(64)]
        self.goodness_delta = 0.0
    
    def tropical_forward(self, input_vec):
        return max(a + b for a, b in zip(input_vec, self.specialization))
    
    def train_ff(self, positive, negative):
        pos_g = self.tropical_forward(positive)
        neg_g = self.tropical_forward(negative)
        delta = pos_g - neg_g
        self.goodness_delta += delta
        if delta > 0:
            for i in range(64):
                new_val = self.specialization[i] + positive[i]
                self.specialization[i] = max(-1, min(1, new_val))
        return delta

class Router:
    def __init__(self):
        self.experts = {i: ExpertNetwork(i) for i in range(CONFIG["num_experts"])}
        self.utilization = [0] * CONFIG["num_experts"]
        self.total_requests = 0
    
    def route(self, input_vec):
        scores = [(expert.tropical_forward(input_vec), eid) for eid, expert in self.experts.items()]
        scores.sort(reverse=True)
        selected = [eid for _, eid in scores[:CONFIG["active_experts"]]]
        for eid in selected:
            self.utilization[eid] += 1
        self.total_requests += 1
        return {"selected": selected}
    
    def get_stats(self):
        if self.total_requests == 0:
            return {"imbalance": 0.0}
        rates = [u / self.total_requests for u in self.utilization]
        mean = 1.0 / CONFIG["num_experts"]
        var = sum((r - mean) ** 2 for r in rates) / len(rates)
        return {"imbalance": var ** 0.5}

def init_flash_cim():
    path = CONFIG["flash_cim_path"]
    for subdir in ["blocks", "metadata", "checkpoints", "models"]:
        os.makedirs(os.path.join(path, subdir), exist_ok=True)
    return True

def generate_sample(i):
    pattern = i % 16
    sample = []
    for j in range(64):
        if pattern == 0: bias = 1 if j % 3 == 0 else 0
        elif pattern == 1: bias = -1 if j % 3 == 1 else 0
        elif pattern == 2: bias = 1 if j % 4 < 2 else -1
        elif pattern == 3: bias = 1 if j > 32 else -1
        elif pattern == 4: bias = 1 if j % 5 == 0 else 0
        elif pattern == 5: bias = 1 if (j // 8) % 2 == 0 else -1
        elif pattern == 6: bias = 1 if j % 7 < 3 else 0
        else: bias = random.choice([-1, 0, 1])
        val = random.choice([-1, 0, 1]) + bias
        sample.append(max(-1, min(1, val)))
    return sample

def process_batch(args):
    batch, expert_specializations = args
    local_updates = {eid: [0]*64 for eid in range(243)}
    local_utilization = [0]*243
    total_delta = 0.0
    
    for s in batch:
        # Route: find top 16 experts
        scores = []
        for eid in range(243):
            score = max(a + b for a, b in zip(s, expert_specializations[eid]))
            scores.append((score, eid))
        scores.sort(reverse=True)
        selected = [eid for _, eid in scores[:16]]
        
        for eid in selected:
            local_utilization[eid] += 1
            # Generate negative
            neg = s.copy()
            for _ in range(6):
                neg[random.randint(0, 63)] = random.choice([-1, 0, 1])
            # Train
            pos_g = max(a + b for a, b in zip(s, expert_specializations[eid]))
            neg_g = max(a + b for a, b in zip(neg, expert_specializations[eid]))
            delta = pos_g - neg_g
            total_delta += delta
            if delta > 0:
                for i in range(64):
                    local_updates[eid][i] += s[i]
    
    return local_updates, local_utilization, total_delta, len(batch)

def train_epoch_parallel(router):
    indices = list(range(CONFIG["training_samples"]))
    random.shuffle(indices)
    
    # Create batches
    batches = []
    batch_size = CONFIG["batch_size"]
    for i in range(0, len(indices), batch_size):
        batch_indices = indices[i:i+batch_size]
        batch = [generate_sample(idx) for idx in batch_indices]
        # Get current expert weights
        expert_specs = {eid: router.experts[eid].specialization[:] for eid in range(243)}
        batches.append((batch, expert_specs))
    
    # Process in parallel
    total_delta = 0.0
    total_samples = 0
    
    with Pool(CONFIG["workers"]) as pool:
        results = pool.map(process_batch, batches)
    
    # Aggregate results
    for local_updates, local_util, batch_delta, batch_count in results:
        total_delta += batch_delta
        total_samples += batch_count
        for eid in range(243):
            router.utilization[eid] += local_util[eid]
            # Apply weight updates
            for i in range(64):
                if local_updates[eid][i] != 0:
                    new_val = router.experts[eid].specialization[i] + local_updates[eid][i]
                    router.experts[eid].specialization[i] = max(-1, min(1, new_val))
    
    router.total_requests += total_samples
    avg_delta = total_delta / (total_samples * CONFIG["active_experts"]) if total_samples > 0 else 0
    stats = router.get_stats()
    return {"delta": avg_delta, "imbalance": stats["imbalance"]}

def main():
    print(f"STRESS TEST: {CONFIG['max_epochs']} epochs, {CONFIG['training_samples']} samples, {CONFIG['workers']} workers")
    init_flash_cim()
    router = Router()
    start = time.time()
    
    for epoch in range(CONFIG["max_epochs"]):
        e_start = time.time()
        m = train_epoch_parallel(router)
        e_time = time.time() - e_start
        print(f"Epoch {epoch}/{CONFIG['max_epochs']}: d={m['delta']:.4f} b={m['imbalance']:.4f} t={e_time:.1f}s")
        
        if (epoch + 1) % 100 == 0:
            path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", f"e_{epoch+1}.json")
            ew = {str(eid): ex.specialization for eid, ex in router.experts.items()}
            with open(path, "w") as f:
                json.dump({"epoch": epoch+1, "metrics": m, "weights": ew}, f)
            print(f"  Checkpoint saved: {path}")
    
    total_time = time.time() - start
    print(f"\\nDone: {total_time/3600:.2f}h")
    
    # Save final
    final_path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", "final_model.json")
    ew = {str(eid): ex.specialization for eid, ex in router.experts.items()}
    with open(final_path, "w") as f:
        json.dump({"config": CONFIG, "metrics": m, "weights": ew}, f)
    print(f"Final: {final_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
'''

with open('stress_optimized.py', 'w') as f:
    f.write(script)
print('Optimized stress test created')
