import os

script_content = '''#!/usr/bin/env python3
import os, sys, json, random, time
from datetime import datetime

CONFIG = {
    "flash_cim_path": os.getenv("FLASH_CIM_PATH", "D:\\\\flash_cim_243expert_stress"),
    "num_experts": 243,
    "active_experts": 16,
    "training_samples": 500000,
    "max_epochs": 1000,
    "batch_size": 256,
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
        elif pattern == 7: bias = 1 if (j // 4) % 3 == 0 else -1 if (j // 4) % 3 == 1 else 0
        elif pattern == 8: bias = 1 if j % 11 < 4 else -1 if j % 11 > 7 else 0
        elif pattern == 9: bias = 1 if j < 20 or j > 44 else -1
        elif pattern == 10: bias = 1 if j % 6 in [0, 1] else -1 if j % 6 in [3, 4] else 0
        elif pattern == 11: bias = -1 if (j // 2) % 2 == 0 else 1
        elif pattern == 12: bias = 1 if j % 8 == 0 else -1 if j % 8 == 4 else 0
        elif pattern == 13: bias = 1 if j % 9 in [0, 1, 2] else -1 if j % 9 in [5, 6, 7] else 0
        elif pattern == 14: bias = -1 if j < 16 else 1 if j > 48 else 0
        else: bias = random.choice([-1, 0, 1])
        val = random.choice([-1, 0, 1]) + bias
        sample.append(max(-1, min(1, val)))
    return sample

def train_epoch(router):
    total_delta = 0.0
    sample_count = 0
    indices = list(range(CONFIG["training_samples"]))
    random.shuffle(indices)
    batch = []
    for idx in indices:
        sample = generate_sample(idx)
        batch.append(sample)
        if len(batch) >= CONFIG["batch_size"]:
            for s in batch:
                routing = router.route(s)
                neg = s.copy()
                for _ in range(6):
                    neg[random.randint(0, 63)] = random.choice([-1, 0, 1])
                for eid in routing["selected"]:
                    expert = router.experts[eid]
                    delta = expert.train_ff(s, neg)
                    total_delta += delta
                sample_count += 1
            batch = []
    for s in batch:
        routing = router.route(s)
        neg = s.copy()
        for _ in range(6):
            neg[random.randint(0, 63)] = random.choice([-1, 0, 1])
        for eid in routing["selected"]:
            expert = router.experts[eid]
            delta = expert.train_ff(s, neg)
            total_delta += delta
        sample_count += 1
    avg_delta = total_delta / (sample_count * CONFIG["active_experts"]) if sample_count > 0 else 0
    stats = router.get_stats()
    return {"delta": avg_delta, "imbalance": stats["imbalance"]}

def main():
    print("STRESS TEST: 1000 epochs, 500K samples")
    init_flash_cim()
    router = Router()
    start = time.time()
    for epoch in range(CONFIG["max_epochs"]):
        e_start = time.time()
        m = train_epoch(router)
        print(f"Epoch {epoch}/1000: d={m['delta']:.4f} b={m['imbalance']:.4f} t={time.time()-e_start:.1f}s")
        if (epoch + 1) % 100 == 0:
            path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", f"e_{epoch+1}.json")
            ew = {str(eid): ex.specialization for eid, ex in router.experts.items()}
            with open(path, "w") as f:
                json.dump({"epoch": epoch+1, "metrics": m, "weights": ew}, f)
    print(f"Done: {(time.time()-start)/3600:.2f}h")
    return 0

if __name__ == "__main__":
    sys.exit(main())
'''

with open('deploy_243expert_stress.py', 'w') as f:
    f.write(script_content)
print('CREATED')
