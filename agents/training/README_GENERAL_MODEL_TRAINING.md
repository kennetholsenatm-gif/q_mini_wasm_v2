# General Model Training for q_mini_wasm_v2 Framework

This document explains how to train the general model using the q_mini_wasm_v2 framework with a focus on quality over quantity, stem awareness, and model awareness.

## Overview

The general model training system is designed to:
- Train until 99% accuracy is reached
- Use high-quality datasets (up to 48GB)
- Focus on stem and model awareness
- Utilize Flash-CIM integration for efficient ternary data storage

## Training Files

### Core Training Script
- `train_general_model.go` - Main training implementation
- `train_general_model.bat` - Batch file to run training

### Configuration
- `training_config.json` - Training configuration (auto-generated)
- `training_summary.json` - Training results summary (auto-generated)

### Output Directories
- `D:\general_model\training\` - Main training directory
- `D:\general_model\training\datasets\` - Dataset storage
- `D:\general_model\training\metrics\` - Training metrics
- `D:\general_model\training\models\` - Trained model files
- `D:\general_model\training\results\` - Training results

## Training Process

### 1. Prerequisites
- Go programming language (1.19 or higher)
- D:\ drive available for storage
- Flash-CIM integration (optional but recommended)

### 2. Running Training

Execute the batch file:
```batch
cd agents/training
train_general_model.bat
```

### 3. Training Configuration

The training uses the following configuration:
- **Epochs**: 1000 (trains until 99% accuracy)
- **Batch Size**: 32 (smaller for quality)
- **Learning Rate**: 0.001
- **Quality Focus**: Enabled
- **Stem Awareness**: Enabled
- **Model Awareness**: Enabled

### 4. Dataset Preparation

The system automatically prepares a 45GB high-quality dataset. For production use, you can:
1. Download and prepare your own dataset
2. Place it in `D:\general_model\training\datasets\`
3. Modify the `prepareDataset()` function in `train_general_model.go`

### 5. Training Monitoring

During training, you'll see:
- Epoch progress with loss and accuracy
- Quality score (20% higher than accuracy)
- Token generation metrics
- Training duration per epoch

### 6. Training Results

After training completes, you'll find:
- `training_report.txt` - Human-readable report
- `training_summary.json` - Machine-readable summary
- `metrics\epoch_*.json` - Detailed metrics per epoch
- `models\` - Trained model files

## Quality-Focused Training

The training system emphasizes quality through:
- Smaller batch sizes for better generalization
- Stem awareness for foundational understanding
- Model awareness for self-reflection capabilities
- Quality score tracking (accuracy × 1.2)

## Flash-CIM Integration

The system uses Flash-CIM for:
- Efficient ternary data storage
- Model block management
- Fast data access during training

## Troubleshooting

### Common Issues

1. **Build Failed**
   - Ensure Go is installed and in PATH
   - Check Go version (1.19+ required)

2. **Directory Creation Failed**
   - Ensure D:\ drive is available
   - Check permissions

3. **Training Slow**
   - Quality-focused training is intentionally slower
   - Consider reducing epochs for testing

### Performance Optimization

- Increase batch size for faster training (reduces quality)
- Adjust learning rate for faster convergence
- Use SSD instead of HDD for storage

## Next Steps

After training completes:
1. Review the training report
2. Validate model quality
3. Deploy the trained model
4. Monitor performance in production

## Support

For issues and questions:
- Check the training logs in `D:\general_model\training\metrics\`
- Review the error messages during training
- Consult the q_mini_wasm_v2 documentation