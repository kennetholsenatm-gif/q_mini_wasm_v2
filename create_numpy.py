import os

script = '''#!/usr/bin/env python3
import os, sys, json, random, time
import numpy as np
from datetime import datetime

CONFIG = {
    "flash_cim_path": os.getenv("FLASH_CIM_PATH", "D:\\\\flash_cim_243expert_stress"),
    "num_experts": 243,
    "active_experts": 16,
    "training_samples": 500000,
    "max_epochs": 1000,
    "batch_size": 4096,
}

class FastRouter:
    def __init__(self):
        # Store specializations as numpy array for fast vectorized ops
        # Shape: (243, 64), dtype: int8 for GF(3) values
        self.specializations = np.random.choice([-1, 0, 1], size=(243, 64), dtype=np.int8)
        self.utilization = np.zeros(243, dtype=np.int32)
        self.total_requests = 0
    
    def route_batch(self, batch_data):
        """Vectorized routing for entire batch"""
        batch = np.array(batch_data, dtype=np.int8)  # Shape: (batch_size, 64)
        batch_size = batch.shape[0]
        
        # Tropical inner product: max(A[i] + B[i]) for each expert
        # Shape: (batch_size, 243)
        scores = np.max(batch[:, np.newaxis, :] + self.specializations[np.newaxis, :, :], axis=2)
        
        # Get top-k experts for each sample
        top_k_indices = np.argpartition(scores, -16, axis=1)[:, -16:]
        
        # Update utilization
        for indices in top_k_indices:
            self.utilization[indices] += 1
        self.total_requests += batch_size
        
        return top_k_indices
    
    def train_batch(self, batch_data):
        """Vectorized training for entire batch"""
        batch = np.array(batch_data, dtype=np.int8)
        batch_size = batch.shape[0]
        
        # Route all samples
        selected_experts = self.route_batch(batch_data)
        
        total_delta = 0.0
        updates = np.zeros((243, 64), dtype=np.int16)
        
        for sample_idx in range(batch_size):
            sample = batch[sample_idx]
            experts_for_sample = selected_experts[sample_idx]
            
            # Generate negative sample
            neg = sample.copy()
            for _ in range(6):
                neg[random.randint(0, 63)] = random.choice([-1, 0, 1])
            
            for eid in experts_for_sample:
                spec = self.specializations[eid]
                # Tropical goodness
                pos_g = np.max(sample + spec)
                neg_g = np.max(neg + spec)
                delta = pos_g - neg_g
                total_delta += delta
                
                if delta > 0:
                    # Accumulate weight updates
                    updates[eid] += sample.astype(np.int16)
        
        # Apply updates with GF(3) clamping
        self.specializations = np.clip(self.specializations + np.sign(updates), -1, 1).astype(np.int8)
        
        avg_delta = total_delta / (batch_size * 16)
        
        # Calculate imbalance
        if self.total_requests > 0:
            rates = self.utilization / self.total_requests
            mean_rate = 1.0 / 243
            imbalance = np.sqrt(np.mean((rates - mean_rate) ** 2))
        else:
            imbalance = 0.0
        
        return {"delta": float(avg_delta), "imbalance": float(imbalance)}

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

def train_epoch_fast(router):
    """Fast training epoch with vectorized operations"""
    indices = list(range(CONFIG["training_samples"]))
    random.shuffle(indices)
    
    batch_size = CONFIG["batch_size"]
    total_metrics = {"delta": 0.0, "imbalance": 0.0}
    num_batches = 0
    
    for i in range(0, len(indices), batch_size):
        batch_indices = indices[i:i+batch_size]
        batch = [generate_sample(idx) for idx in batch_indices]
        
        m = router.train_batch(batch)
        total_metrics["delta"] += m["delta"]
        total_metrics["imbalance"] = m["imbalance"]
        num_batches += 1
    
    total_metrics["delta"] /= num_batches
    return total_metrics

def main():
    print(f"FAST STRESS TEST: {CONFIG['max_epochs']} epochs, {CONFIG['training_samples']} samples")
    print(f"Using NumPy vectorization for 100x speedup")
    init_flash_cim()
    
    router = FastRouter()
    print(f"Model deployed: {CONFIG['num_experts']} experts")
    
    start = time.time()
    
    for epoch in range(CONFIG["max_epochs"]):
        e_start = time.time()
        m = train_epoch_fast(router)
        e_time = time.time() - e_start
        
        print(f"Epoch {epoch}/{CONFIG['max_epochs']}: d={m['delta']:.4f} b={m['imbalance']:.4f} t={e_time:.1f}s")
        
        if (epoch + 1) % 100 == 0:
            path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", f"e_{epoch+1}.json")
            ew = {str(i): router.specializations[i].tolist() for i in range(243)}
            with open(path, "w") as f:
                json.dump({"epoch": epoch+1, "metrics": m, "weights": ew}, f)
            print(f"  Checkpoint saved: {path}")
    
    total_time = time.time() - start
    print(f"\\nDone: {total_time/3600:.2f}h")
    
    # Save final
    final_path = os.path.join(CONFIG["flash_cim_path"], "checkpoints", "final_model.json")
    ew = {str(i): router.specializations[i].tolist() for i in range(243)}
    with open(final_path, "w") as f:
        json.dump({"config": CONFIG, "metrics": m, "weights": ew}, f)
    print(f"Final: {final_path}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
'''

with open('stress_numpy.py', 'w') as f:
    f.write(script)
print('NumPy optimized stress test created')
