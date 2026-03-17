# Q-Mini-WASM Frontend

A modern, accessible, and performant React frontend for the Q-Mini-WASM project, built with TypeScript, Vite, and a comprehensive design system.

## 🚀 Features

### 🎨 Design System
- **Comprehensive Design Tokens**: CSS-in-JS variables for colors, spacing, typography, and shadows
- **Accessible Components**: WCAG 2.2 compliant Button, Input, Card, Modal, Toast, and Error Boundary components
- **Responsive Design**: Mobile-first approach with breakpoints for all screen sizes
- **Dark Mode Support**: Automatic theme switching with CSS-in-JS variables
- **High Contrast Mode**: Enhanced accessibility for users with visual impairments

### 🔒 Security & Performance
- **Content Security Policy**: Comprehensive CSP headers to prevent XSS attacks
- **Input Validation & Sanitization**: Built-in protection against SQL injection and XSS
- **Secure Storage**: Encrypted localStorage with Web Crypto API
- **Performance Monitoring**: Built-in performance tracking and optimization utilities
- **Lazy Loading**: Image and component lazy loading with Intersection Observer
- **Virtualization**: Long list virtualization for optimal rendering performance

### 🛠 Development Experience
- **TypeScript**: Full type safety with comprehensive type definitions
- **Jest & React Testing Library**: Comprehensive test suite with accessibility testing
- **ESLint & Prettier**: Code quality and formatting standards
- **Hot Module Replacement**: Fast development with Vite
- **Path Aliases**: Clean import paths with `@/` prefix

### 📱 User Experience
- **Toast Notifications**: Non-intrusive user feedback system
- **Error Boundaries**: Graceful error handling with user-friendly messages
- **Loading States**: Comprehensive loading indicators and skeleton screens
- **Keyboard Navigation**: Full keyboard accessibility support
- **Screen Reader Support**: ARIA labels and semantic HTML

## 📦 Installation

### Prerequisites
- Node.js 18+ 
- npm 8+ or yarn 1.22+

### Setup
```bash
# Clone the repository
git clone <repository-url>
cd LLM_Pract/wui/frontend

# Install dependencies
npm install

# Start development server
npm run dev

# Build for production
npm run build

# Run tests
npm test

# Run accessibility tests
npm run test:accessibility

# Lint code
npm run lint

# Format code
npm run format
```

## 🏗️ Project Structure

```
src/
├── components/           # React components
│   ├── ui/              # Design system components
│   │   ├── Button/      # Accessible button component
│   │   ├── Input/       # Form input with validation
│   │   ├── Card/        # Content container component
│   │   ├── Modal/       # Accessible modal dialog
│   │   ├── Toast/       # Notification system
│   │   ├── ErrorBoundary/ # Error handling component
│   │   └── index.ts     # Component exports
│   └── layout/          # Layout components
│       ├── Header/      # Application header
│       ├── Sidebar/     # Navigation sidebar
│       └── Layout/      # Main layout wrapper
├── pages/               # Page components
│   ├── Dashboard/       # Main dashboard
│   ├── Circuits/        # Circuit management
│   ├── Hardware/        # Hardware configuration
│   ├── Quantum/         # Quantum backend management
│   ├── Deployment/      # Deployment configuration
│   └── Training/        # Training job management
├── hooks/               # Custom React hooks
│   ├── useAuth/         # Authentication hook
│   ├── useApi/          # API interaction hook
│   ├── useToast/        # Toast notification hook
│   └── performance.ts   # Performance optimization hooks
├── services/            # API services
│   ├── api.ts           # Main API client
│   └── auth.ts          # Authentication service
├── utils/               # Utility functions
│   ├── formatters.ts    # Data formatting utilities
│   ├── validators.ts    # Input validation utilities
│   ├── performance.ts   # Performance optimization utilities
│   └── accessibility.ts # Accessibility utilities
├── security/            # Security utilities
│   ├── security-headers.ts # CSP and security headers
│   └── csrf.ts          # CSRF protection
├── styles/              # Global styles
│   ├── globals.css      # Global CSS-in-JS variables
│   ├── reset.css        # CSS reset
│   └── theme.css        # Theme-specific styles
├── __tests__/           # Test files
│   ├── accessibility.test.tsx # Accessibility tests
│   └── components/      # Component tests
├── App.tsx              # Main application component
├── main.tsx             # Application entry point
└── vite-env.d.ts        # Vite type definitions
```

## 🎨 Design System

### Color Palette
The design system uses a comprehensive color palette with semantic naming:

- **Primary Colors**: `--color-primary-*` (brand colors)
- **Neutral Colors**: `--color-neutral-*` (grays and text)
- **Success Colors**: `--color-success-*` (positive feedback)
- **Error Colors**: `--color-error-*` (errors and warnings)
- **Warning Colors**: `--color-warning-*` (caution messages)

### Typography
- **Font Family**: System font stack with fallbacks
- **Font Sizes**: 12px to 48px with consistent scaling
- **Line Heights**: 1.2 to 1.8 for optimal readability
- **Font Weights**: 300 to 700 for hierarchy

### Spacing
- **Base Unit**: 4px grid system
- **Spacing Scale**: xs (4px) to xxxl (48px)
- **Consistent Padding/Margin**: Using CSS-in-JS variables

### Shadows
- **Subtle**: For depth without distraction
- **Medium**: For important elements
- **Large**: For modal overlays and focus states

## 🔧 Configuration

### Environment Variables
Create a `.env` file in the frontend directory:

```env
VITE_API_BASE_URL=http://localhost:8080
VITE_WS_URL=ws://localhost:8080/ws
VITE_APP_NAME=Q-Mini-WASM
VITE_APP_VERSION=1.0.0
VITE_ENABLE_DEBUG=true
```

### Vite Configuration
The `vite.config.ts` file includes:
- TypeScript support
- CSS-in-JS variables
- Path aliases (`@/` prefix)
- Development server configuration
- Build optimization settings

### ESLint & Prettier
- **ESLint**: TypeScript and React rules
- **Prettier**: Consistent code formatting
- **Husky**: Pre-commit hooks for code quality

## 🧪 Testing

### Running Tests
```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run accessibility tests
npm run test:accessibility

# Generate coverage report
npm run test:coverage
```

### Test Structure
- **Unit Tests**: Component functionality
- **Integration Tests**: API interactions
- **Accessibility Tests**: WCAG 2.2 compliance
- **Performance Tests**: Load time and rendering tests

### Test Utilities
- **React Testing Library**: Component testing
- **Jest**: Test framework with accessibility matchers
- **MSW**: Mock Service Worker for API mocking

## 🔒 Security

### Content Security Policy
The application implements a strict CSP:
- **Script Sources**: Self-hosted only
- **Style Sources**: Self and Google Fonts
- **Connect Sources**: API endpoints only
- **Image Sources**: Self, data URIs, and HTTPS

### Input Validation
- **XSS Protection**: HTML entity encoding
- **SQL Injection**: Pattern detection and prevention
- **CSRF Protection**: Token-based protection
- **Rate Limiting**: Built-in rate limiting utilities

### Secure Storage
- **Encrypted localStorage**: Web Crypto API encryption
- **Fallback Support**: Graceful degradation for older browsers
- **Automatic Cleanup**: Memory management utilities

## 📈 Performance

### Optimization Features
- **Code Splitting**: Route-based chunking
- **Lazy Loading**: Component and image lazy loading
- **Virtualization**: Long list optimization
- **Caching**: Intelligent caching strategies
- **Bundle Analysis**: Build size optimization

### Performance Monitoring
- **Execution Time**: Function performance tracking
- **Memory Usage**: Memory leak detection
- **Render Performance**: Component render optimization
- **Network Performance**: API call optimization

### Bundle Size
- **Tree Shaking**: Unused code elimination
- **Compression**: Gzip and Brotli compression
- **Asset Optimization**: Image and font optimization

## 🎯 Accessibility

### WCAG 2.2 Compliance
- **Keyboard Navigation**: Full keyboard support
- **Screen Reader Support**: ARIA labels and semantic HTML
- **Color Contrast**: High contrast mode support
- **Focus Management**: Logical tab order and focus indicators
- **Error Handling**: Accessible error messages

### Testing
- **Automated Testing**: Jest-axe integration
- **Manual Testing**: Screen reader testing
- **Keyboard Testing**: Tab navigation testing
- **Contrast Testing**: Color contrast validation

## 🚀 Deployment

### Build Process
```bash
# Build for production
npm run build

# Preview build
npm run preview

# Analyze bundle
npm run analyze
```

### Production Considerations
- **Environment Variables**: Secure handling in production
- **CSP Headers**: Server-side CSP implementation
- **HTTPS**: Required for security features
- **Caching**: Proper cache headers for static assets

### CI/CD Integration
The frontend includes configuration for:
- **GitHub Actions**: Automated testing and deployment
- **Docker**: Containerized deployment
- **Netlify/Vercel**: Static site deployment

## 🤝 Contributing

### Development Workflow
1. **Fork** the repository
2. **Clone** your fork
3. **Create** a feature branch
4. **Make** your changes
5. **Test** your changes
6. **Commit** with descriptive messages
7. **Push** to your fork
8. **Create** a pull request

### Code Standards
- **TypeScript**: All code must be typed
- **Accessibility**: All components must be accessible
- **Performance**: Optimize for performance
- **Security**: Follow security best practices
- **Testing**: Write tests for new features

### Component Guidelines
- **Props Interface**: Define TypeScript interfaces
- **Accessibility**: Include ARIA attributes
- **Error Handling**: Handle errors gracefully
- **Loading States**: Include loading indicators
- **Documentation**: Document component usage

## 📚 Documentation

### Component Documentation
Each component includes:
- **Props Interface**: TypeScript type definitions
- **Usage Examples**: Code examples
- **Accessibility Notes**: ARIA and keyboard support
- **Performance Notes**: Optimization considerations

### API Documentation
- **Type Definitions**: Complete TypeScript interfaces
- **Error Handling**: Error response types
- **Authentication**: Auth flow documentation
- **Rate Limiting**: API usage limits

## 🔗 Integration

### Backend API
The frontend integrates with the Q-Mini-WASM backend API:
- **RESTful Endpoints**: Standard HTTP methods
- **WebSocket Support**: Real-time updates
- **Authentication**: JWT-based auth
- **Error Handling**: Consistent error responses

### External Services
- **Quantum Backends**: Integration with quantum computing services
- **Hardware Monitoring**: Real-time hardware status
- **Deployment Targets**: Multiple deployment platforms

## 🐛 Troubleshooting

### Common Issues
- **CSP Errors**: Check Content Security Policy headers
- **CORS Errors**: Verify API endpoint configuration
- **Performance Issues**: Use performance monitoring tools
- **Accessibility Issues**: Run accessibility tests

### Debug Tools
- **Browser DevTools**: Network and performance tabs
- **React DevTools**: Component inspection
- **Accessibility Tools**: Screen reader testing
- **Performance Tools**: Lighthouse and WebPageTest

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## 🙏 Acknowledgments

- **React Ecosystem**: For the excellent component model
- **TypeScript**: For type safety and developer experience
- **Vite**: For fast development and build times
- **Accessibility Community**: For WCAG guidelines and best practices
- **Security Community**: For security best practices and tools

## 📞 Support

For support and questions:
- **GitHub Issues**: Bug reports and feature requests
- **Documentation**: Comprehensive guides and examples
- **Community**: Join our discussions and contribute

---

**Built with ❤️ for the quantum computing community**