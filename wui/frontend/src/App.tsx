import React, { useState } from 'react';
import './App.css';
import { Button, Input, Card, CardHeader, CardContent, CardFooter, Modal } from './components/ui';

function App() {
  const [isModalOpen, setIsModalOpen] = useState(false);
  const [formData, setFormData] = useState({
    name: '',
    email: '',
    message: ''
  });
  const [errors, setErrors] = useState({
    name: '',
    email: '',
    message: ''
  });

  const handleInputChange = (e: React.ChangeEvent<HTMLInputElement | HTMLTextAreaElement>) => {
    const { name, value } = e.target;
    setFormData(prev => ({
      ...prev,
      [name]: value
    }));
    
    // Clear error when user starts typing
    if (errors[name as keyof typeof errors]) {
      setErrors(prev => ({
        ...prev,
        [name]: ''
      }));
    }
  };

  const validateForm = () => {
    const newErrors = {
      name: '',
      email: '',
      message: ''
    };
    let isValid = true;

    if (!formData.name.trim()) {
      newErrors.name = 'Name is required';
      isValid = false;
    }

    if (!formData.email.trim()) {
      newErrors.email = 'Email is required';
      isValid = false;
    } else if (!/\S+@\S+\.\S+/.test(formData.email)) {
      newErrors.email = 'Email is invalid';
      isValid = false;
    }

    if (!formData.message.trim()) {
      newErrors.message = 'Message is required';
      isValid = false;
    }

    setErrors(newErrors);
    return isValid;
  };

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    
    if (validateForm()) {
      console.log('Form submitted:', formData);
      // Reset form
      setFormData({ name: '', email: '', message: '' });
      setIsModalOpen(true);
    }
  };

  return (
    <div className="app">
      <header className="app-header">
        <div className="container">
          <h1>Q-Mini-WASM Design System</h1>
          <p className="app-subtitle">
            A comprehensive design system implementation following WCAG 2.2 accessibility standards
            and modern UI/UX principles.
          </p>
        </div>
      </header>

      <main className="app-main">
        <div className="container">
          <div className="grid grid-cols-1 lg:grid-cols-2 gap-8">
            {/* Component Showcase */}
            <Card variant="elevated" className="component-showcase">
              <CardHeader border>
                <h2>Component Library</h2>
                <div className="component-actions">
                  <Button variant="outline" size="sm">
                    View All
                  </Button>
                </div>
              </CardHeader>
              
              <CardContent>
                <div className="component-grid">
                  <div className="component-item">
                    <h3>Buttons</h3>
                    <div className="component-demo">
                      <Button variant="primary" className="mr-2">Primary</Button>
                      <Button variant="secondary" className="mr-2">Secondary</Button>
                      <Button variant="success" className="mr-2">Success</Button>
                      <Button variant="warning" className="mr-2">Warning</Button>
                      <Button variant="error" className="mr-2">Error</Button>
                      <Button variant="ghost" className="mr-2">Ghost</Button>
                      <Button variant="outline">Outline</Button>
                    </div>
                  </div>

                  <div className="component-item">
                    <h3>Button Sizes</h3>
                    <div className="component-demo">
                      <Button size="sm" className="mr-2">Small</Button>
                      <Button size="md" className="mr-2">Medium</Button>
                      <Button size="lg">Large</Button>
                    </div>
                  </div>

                  <div className="component-item">
                    <h3>Inputs</h3>
                    <div className="component-demo">
                      <div style={{ maxWidth: '300px' }}>
                        <Input
                          label="Name"
                          placeholder="Enter your name"
                          error={errors.name}
                          fullWidth
                        />
                      </div>
                    </div>
                  </div>

                  <div className="component-item">
                    <h3>Card Variants</h3>
                    <div className="component-demo">
                      <div style={{ display: 'flex', gap: '1rem', flexWrap: 'wrap' }}>
                        <Card variant="default" padding="sm" style={{ width: '150px', height: '100px' }}>
                          Default
                        </Card>
                        <Card variant="elevated" padding="sm" style={{ width: '150px', height: '100px' }}>
                          Elevated
                        </Card>
                        <Card variant="outlined" padding="sm" style={{ width: '150px', height: '100px' }}>
                          Outlined
                        </Card>
                      </div>
                    </div>
                  </div>
                </div>
              </CardContent>
              
              <CardFooter border>
                <div className="component-footer">
                  <span className="component-status">All components are WCAG 2.2 compliant</span>
                  <div className="component-links">
                    <Button variant="ghost" size="sm">Documentation</Button>
                    <Button variant="ghost" size="sm">Accessibility</Button>
                  </div>
                </div>
              </CardFooter>
            </Card>

            {/* Interactive Demo */}
            <Card variant="default" className="interactive-demo">
              <CardHeader>
                <h2>Interactive Form Demo</h2>
              </CardHeader>
              
              <CardContent>
                <form onSubmit={handleSubmit} className="demo-form">
                  <div className="form-group">
                    <Input
                      label="Full Name"
                      name="name"
                      value={formData.name}
                      onChange={handleInputChange}
                      error={errors.name}
                      placeholder="Enter your full name"
                      fullWidth
                    />
                  </div>

                  <div className="form-group">
                    <Input
                      label="Email Address"
                      name="email"
                      type="email"
                      value={formData.email}
                      onChange={handleInputChange}
                      error={errors.email}
                      placeholder="your.email@example.com"
                      fullWidth
                    />
                  </div>

                  <div className="form-group">
                    <Input
                      label="Message"
                      name="message"
                      value={formData.message}
                      onChange={handleInputChange}
                      error={errors.message}
                      placeholder="Enter your message"
                      fullWidth
                    />
                  </div>

                  <div className="form-actions">
                    <Button type="submit" variant="primary" size="lg">
                      Submit Form
                    </Button>
                    <Button type="button" variant="ghost" onClick={() => setIsModalOpen(true)}>
                      Open Modal
                    </Button>
                  </div>
                </form>
              </CardContent>
            </Card>
          </div>

          {/* Features Grid */}
          <div className="features-grid">
            <Card variant="elevated">
              <CardHeader>
                <h3>Accessibility First</h3>
              </CardHeader>
              <CardContent>
                <p>All components are built with WCAG 2.2 AA compliance in mind, ensuring your application
                is accessible to all users including those using screen readers and keyboard navigation.</p>
              </CardContent>
            </Card>

            <Card variant="elevated">
              <CardHeader>
                <h3>Responsive Design</h3>
              </CardHeader>
              <CardContent>
                <p>Components automatically adapt to different screen sizes and devices, providing an
                optimal user experience across desktop, tablet, and mobile devices.</p>
              </CardContent>
            </Card>

            <Card variant="elevated">
              <CardHeader>
                <h3>Dark Mode Support</h3>
              </CardHeader>
              <CardContent>
                <p>Automatic dark mode support with carefully crafted color palettes that maintain
                excellent contrast and readability in both light and dark themes.</p>
              </CardContent>
            </Card>
          </div>
        </div>
      </main>

      <Modal
        isOpen={isModalOpen}
        onClose={() => setIsModalOpen(false)}
        title="Form Submitted Successfully"
        size="md"
      >
        <p>Thank you for your submission! Your form has been successfully processed.</p>
        <div style={{ marginTop: 'var(--spacing-lg)', display: 'flex', justifyContent: 'flex-end' }}>
          <Button variant="primary" onClick={() => setIsModalOpen(false)}>
            Close
          </Button>
        </div>
      </Modal>
    </div>
  );
}

export default App;