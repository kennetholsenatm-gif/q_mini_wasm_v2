import os

# Read the file
with open('deploy_243expert_stress.py', 'r') as f:
    content = f.read()

# Fix the docstring - replace 500K with 500k to avoid syntax error
content = content.replace(
    '"""Streaming data generator for 500K samples (memory efficient)"""',
    '"""Streaming data generator for 500k samples (memory efficient)"""'
)

# Also fix the module docstring
content = content.replace(
    '"""243-Expert MoE Deep Convergence Stress Test\n\n1000 epochs, 500K samples, validation every 100 epochs\nUses D:\\ as Flash-CiM storage\n"""',
    '"""243-Expert MoE Deep Convergence Stress Test\n\n1000 epochs, 500k samples, validation every 100 epochs\nUses Flash-CiM storage\n"""'
)

# Write back
with open('deploy_243expert_stress.py', 'w') as f:
    f.write(content)

print('Fixed docstrings')
