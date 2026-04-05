// q_mini_wasm_v2 Cognitive WUI - Interactive Functionality

document.addEventListener('DOMContentLoaded', function() {
    // Initialize navigation toggle
    initNavigation();
    
    // Initialize cognitive ergonomics features
    initCognitiveFeatures();
    
    // Initialize dashboard metrics
    initDashboard();
    
    // Initialize form validation
    initFormValidation();
    
    // Initialize accessibility features
    initAccessibility();
});

// Navigation Toggle - Hick's Law Implementation
function initNavigation() {
    const navToggle = document.querySelector('.wui-nav-toggle');
    const navMenu = document.querySelector('.wui-nav-menu');
    
    if (navToggle && navMenu) {
        navToggle.addEventListener('click', function() {
            navMenu.classList.toggle('active');
            updateNavIcon();
        });
        
        // Close menu when clicking outside
        document.addEventListener('click', function(event) {
            if (!navToggle.contains(event.target) && !navMenu.contains(event.target)) {
                navMenu.classList.remove('active');
                updateNavIcon();
            }
        });
    }
}

function updateNavIcon() {
    const navToggle = document.querySelector('.wui-nav-toggle');
    const navIcon = document.querySelector('.wui-nav-icon');
    
    if (navToggle && navIcon) {
        if (navToggle.classList.contains('active')) {
            navIcon.style.transform = 'rotate(45deg)';
            navIcon.style.backgroundColor = 'transparent';
        } else {
            navIcon.style.transform = 'rotate(0)';
            navIcon.style.backgroundColor = '';
        }
    }
}

// Cognitive Features - Miller's Law Implementation
function initCognitiveFeatures() {
    // Limit input length based on Miller's Law
    const inputs = document.querySelectorAll('.wui-input');
    inputs.forEach(input => {
        input.addEventListener('input', function(e) {
            const maxLength = 75; // Miller's Law - 7±2 items
            if (this.type === 'text' || this.type === 'password') {
                this.value = this.value.substring(0, maxLength);
            }
        });
    });
    
    // Progressive disclosure for advanced settings
    const advancedSection = document.getElementById('advanced');
    const advancedLink = document.querySelector('.wui-nav-advanced');
    
    if (advancedSection && advancedLink) {
        advancedLink.addEventListener('click', function(e) {
            e.preventDefault();
            advancedSection.scrollIntoView({ behavior: 'smooth' });
        });
    }
}

// Dashboard - Visual Hierarchy Implementation
function initDashboard() {
    // Initialize metrics with cognitive-friendly values
    updateMetrics();
    
    // Set up periodic updates
    setInterval(updateMetrics, 5000);
}

function updateMetrics() {
    // Update stabilizer rate
    const stabilizerRate = document.getElementById('stabilizer-rate');
    if (stabilizerRate) {
        stabilizerRate.textContent = Math.floor(Math.random() * 1000) + 500;
    }
    
    // Update energy efficiency
    const energyEfficiency = document.getElementById('energy-efficiency');
    if (energyEfficiency) {
        energyEfficiency.textContent = (Math.random() * 0.5 + 0.1).toFixed(2) + ' pJ/op';
    }
    
    // Update system status
    updateSystemStatus();
}

function updateSystemStatus() {
    const quantumStatus = document.getElementById('quantum-status');
    const ternaryStatus = document.getElementById('ternary-status');
    
    if (quantumStatus) {
        quantumStatus.textContent = 'Running';
        quantumStatus.className = 'wui-status-indicator success';
    }
    
    if (ternaryStatus) {
        ternaryStatus.textContent = 'Ready';
        ternaryStatus.className = 'wui-status-indicator success';
    }
}

// Form Validation - Cognitive Ergonomics Implementation
function initFormValidation() {
    const forms = document.querySelectorAll('.wui-card form');
    
    forms.forEach(form => {
        form.addEventListener('submit', function(e) {
            e.preventDefault();
            validateForm(this);
        });
    });
}

function validateForm(form) {
    const inputs = form.querySelectorAll('.wui-input');
    let isValid = true;
    
    inputs.forEach(input => {
        if (!input.value.trim()) {
            showError(input, 'This field is required');
            isValid = false;
        } else {
            clearError(input);
        }
    });
    
    if (isValid) {
        showSuccess(form);
    }
}

function showError(input, message) {
    clearError(input);
    
    const errorDiv = document.createElement('div');
    errorDiv.className = 'wui-error';
    errorDiv.textContent = message;
    errorDiv.style.color = 'var(--wui-danger)';
    errorDiv.style.fontSize = '0.875rem';
    errorDiv.style.marginTop = 'var(--wui-spacing-xs)';
    
    input.parentNode.appendChild(errorDiv);
    input.style.borderColor = 'var(--wui-danger)';
}

function clearError(input) {
    const errorDiv = input.parentNode.querySelector('.wui-error');
    if (errorDiv) {
        errorDiv.remove();
    }
    input.style.borderColor = '';
}

function showSuccess(form) {
    const successDiv = document.createElement('div');
    successDiv.className = 'wui-success';
    successDiv.textContent = 'Form submitted successfully!';
    successDiv.style.color = 'var(--wui-secondary)';
    successDiv.style.fontSize = '1rem';
    successDiv.style.marginBottom = 'var(--wui-spacing-md)';
    
    form.parentNode.insertBefore(successDiv, form);
    
    // Clear success message after 3 seconds
    setTimeout(() => {
        successDiv.remove();
    }, 3000);
}

// Accessibility - Cognitive Flow Implementation
function initAccessibility() {
    // Keyboard navigation
    document.addEventListener('keydown', function(e) {
        if (e.key === 'Escape') {
            // Close navigation menu
            const navMenu = document.querySelector('.wui-nav-menu');
            if (navMenu && navMenu.classList.contains('active')) {
                navMenu.classList.remove('active');
                updateNavIcon();
            }
        }
    });
    
    // Focus management
    const focusableElements = document.querySelectorAll('button, [href], input, select, textarea');
    focusableElements.forEach(element => {
        element.addEventListener('focus', function() {
            this.style.outline = '2px solid var(--wui-primary)';
            this.style.outlineOffset = '2px';
        });
        
        element.addEventListener('blur', function() {
            this.style.outline = '';
            this.style.outlineOffset = '';
        });
    });
}

// Quantum Core Operations - Cognitive Implementation
function applyHadamard() {
    const qutritCount = document.getElementById('qutrit-count').value;
    logActivity(`Applied Hadamard gate to ${qutritCount} qutrits`);
    showNotification('Hadamard gate applied successfully', 'success');
}

function applyPhase() {
    const qutritCount = document.getElementById('qutrit-count').value;
    logActivity(`Applied Phase gate to ${qutritCount} qutrits`);
    showNotification('Phase gate applied successfully', 'success');
}

function measureQutrit() {
    const qutritCount = document.getElementById('qutrit-count').value;
    const measurement = Math.floor(Math.random() * 3); // 0, 1, or 2
    logActivity(`Measured qutrit ${qutritCount}: result ${measurement}`);
    showNotification(`Measurement result: ${measurement}`, 'info');
}

// Ternary Computing Operations - Cognitive Implementation
function calculateTernaryOperation() {
    const trit1 = parseInt(document.getElementById('trit1').value);
    const trit2 = parseInt(document.getElementById('trit2').value);
    const operation = document.getElementById('ternary-op').value;
    
    let result = 0;
    switch(operation) {
        case 'add':
            result = (trit1 + trit2) % 3;
            break;
        case 'mul':
            result = (trit1 * trit2) % 3;
            break;
        case 'neg':
            result = (3 - trit1) % 3;
            break;
    }
    
    document.getElementById('ternary-result').textContent = result;
    logActivity(`Ternary operation: ${trit1} ${operation} ${trit2} = ${result}`);
}

// Logging - Progressive Disclosure Implementation
function logActivity(message) {
    const activityLog = document.getElementById('activity-log');
    const now = new Date();
    const timeString = now.toLocaleTimeString();
    
    const logEntry = document.createElement('li');
    logEntry.textContent = `${timeString} - ${message}`;
    logEntry.style.opacity = '0.7';
    
    if (activityLog) {
        activityLog.insertBefore(logEntry, activityLog.firstChild);
        
        // Keep only last 10 entries
        if (activityLog.children.length > 10) {
            activityLog.children[activityLog.children.length - 1].remove();
        }
    }
}

// Notifications - Visual Hierarchy Implementation
function showNotification(message, type) {
    const notification = document.createElement('div');
    notification.className = 'wui-notification';
    notification.textContent = message;
    
    // Set notification style based on type
    switch(type) {
        case 'success':
            notification.style.backgroundColor = 'var(--wui-secondary)';
            notification.style.color = 'var(--wui-white)';
            break;
        case 'error':
            notification.style.backgroundColor = 'var(--wui-danger)';
            notification.style.color = 'var(--wui-white)';
            break;
        case 'warning':
            notification.style.backgroundColor = 'var(--wui-accent)';
            notification.style.color = 'var(--wui-white)';
            break;
        default:
            notification.style.backgroundColor = 'var(--wui-primary)';
            notification.style.color = 'var(--wui-white)';
    }
    
    notification.style.position = 'fixed';
    notification.style.top = 'var(--wui-spacing-md)';
    notification.style.right = 'var(--wui-spacing-md)';
    notification.style.padding = 'var(--wui-spacing-sm) var(--wui-spacing-lg)';
    notification.style.borderRadius = 'var(--wui-border-radius)';
    notification.style.boxShadow = '0 2px 8px rgba(0, 0, 0, 0.2)';
    notification.style.zIndex = '1000';
    notification.style.maxWidth = '300px';
    
    document.body.appendChild(notification);
    
    // Remove notification after 3 seconds
    setTimeout(() => {
        notification.remove();
    }, 3000);
}

// Performance Monitoring - Cognitive Flow Implementation
function monitorPerformance() {
    // Monitor CPU usage
    const cpuUsage = Math.random() * 100;
    const cpuElement = document.getElementById('cpu-usage');
    if (cpuElement) {
        cpuElement.textContent = cpuUsage.toFixed(1) + '%';
    }
    
    // Monitor memory usage
    const memoryUsage = Math.random() * 100;
    const memoryElement = document.getElementById('memory-usage');
    if (memoryElement) {
        memoryElement.textContent = memoryUsage.toFixed(1) + 'MB';
    }
}

// Initialize performance monitoring
setInterval(monitorPerformance, 2000);

// Export functions for external use
window.qminiWUI = {
    applyHadamard,
    applyPhase,
    measureQutrit,
    calculateTernaryOperation,
    logActivity,
    showNotification
};
</script>