# Q-Mini-WASM Web Interface (WUI)

The Q-Mini-WASM Web Interface is a modern, accessible, and responsive frontend built with React and TypeScript. It implements a comprehensive design system following WCAG 2.2 accessibility standards and modern UI/UX principles.

## 🎨 Design System

Our design system is built on the following principles:

### 🎯 Core Principles

- **Accessibility First**: WCAG 2.2 AA compliance across all components
- **Responsive Design**: Optimized for desktop, tablet, and mobile devices
- **Consistent Branding**: Cohesive visual language with primary and secondary brand colors
- **Performance**: Optimized for fast loading and smooth interactions
- **Dark Mode Support**: Automatic theme switching with carefully crafted palettes

### 🎨 Color System

Our color system is based on semantic color naming with automatic dark mode support:

- **Primary Colors**: Blue theme (`#0ea5e9`) for main brand elements
- **Secondary Colors**: Orange theme (`#f59e0b`) for accent elements  
- **Neutral Colors**: 10-step grayscale for text and backgrounds
- **Semantic Colors**: Success, warning, and error states with proper contrast

### 📐 Typography

- **Base Font**: System font stack for optimal readability
- **Font Sizes**: Rem-based scaling from 12px to 48px
- **Line Heights**: Optimized for readability (1.25 to 1.625)
- **Font Weights**: 300 to 700 for proper hierarchy

### 📏 Spacing System

- **Base Unit**: 4px grid system
- **Scale**: xs (4px) to 8xl (128px)
- **Consistent Padding/Margin**: Predictable spacing across components

## 🧩 Component Library

### Core Components

#### Button
- **Variants**: primary, secondary, success, warning, error, ghost, outline
- **Sizes**: sm (32px), md (40px), lg (48px)
- **Features**: Loading states, icons, full-width option
- **Accessibility**: Focus management, ARIA labels, keyboard navigation

#### Input
- **Types**: text, email, password, number, search
- **Sizes**: sm, md, lg with consistent heights
- **Features**: Labels, helper text, error states, icons
- **Accessibility**: Proper labeling, focus states, validation feedback

#### Card
- **Variants**: default, elevated, outlined
- **Sizes**: Configurable padding (sm, md, lg, none)
- **Features**: Header, content, footer sections
- **Accessibility**: Semantic structure, focus management

#### Modal
- **Sizes**: sm, md, lg, xl, full
- **Features**: Focus trapping, keyboard navigation, overlay click handling
- **Accessibility**: ARIA roles, focus management, screen reader support

### Component Features

- **TypeScript Support**: Full type safety with comprehensive interfaces
- **Accessibility**: WCAG 2.2 AA compliance with proper ARIA attributes
- **Responsive**: Mobile-first design with breakpoint-based adjustments
- **Theming**: Automatic dark mode support with high contrast options
- **Performance**: Optimized rendering with minimal re-renders

## 🚀 Getting Started

### Prerequisites

- Node.js 18+ 
- npm or yarn
- Modern browser with ES6+ support

### Installation

```bash
cd wui/frontend
npm install
```

### Development

```bash
# Start development server
npm run dev

# Build for production
npm run build

# Run tests
npm run test

# Lint code
npm run lint
```

### Project Structure

```
src/
├── components/
│   ├── ui/           # Design system components
│   │   ├── Button/   # Button component
│   │   ├── Input/    # Input component
│   │   ├── Card/     # Card component
│   │   ├── Modal/    # Modal component
│   │   └── index.ts  # Component exports
│   └── App.tsx       # Main application component
├── styles/
│   ├── design-tokens.css  # CSS custom properties
│   ├── base.css           # Base styles and utilities
│   └── App.css            # Application styles
├── App.tsx           # Main application
└── main.tsx          # Application entry point
```

## 🎨 Customization

### Design Tokens

All design tokens are defined in `src/styles/design-tokens.css`:

```css
:root {
  /* Colors */
  --color-primary-500: #0ea5e9;
  --color-secondary-500: #f59e0b;
  
  /* Typography */
  --font-size-base: 1rem;
  --line-height-normal: 1.5;
  
  /* Spacing */
  --spacing-md: 1rem;
  
  /* Shadows */
  --shadow-md: 0 4px 6px -1px rgba(0, 0, 0, 0.1);
}
```

### Component Theming

Components automatically adapt to:

- **Color Scheme**: Light/dark mode via `prefers-color-scheme`
- **Contrast**: High contrast mode via `prefers-contrast`
- **Motion**: Reduced motion via `prefers-reduced-motion`
- **Breakpoints**: Responsive design via media queries

### Creating New Components

1. **Follow the pattern**: Create `.tsx` and `.css` files
2. **Use design tokens**: Reference CSS custom properties
3. **Add TypeScript**: Define proper interfaces
4. **Ensure accessibility**: Add ARIA attributes and keyboard support
5. **Test responsiveness**: Verify mobile and desktop layouts

## 🔧 API Integration

The frontend is designed to work with the Q-Mini-WASM backend API:

### Environment Configuration

```env
VITE_API_BASE_URL=http://localhost:8080
VITE_WS_URL=ws://localhost:8080/ws
VITE_THEME=auto
```

### API Client

```typescript
import { apiClient } from './services/api';

// Example usage
const response = await apiClient.get('/api/status');
```

### Error Handling

- **Global error boundary** for component errors
- **Toast notifications** for user feedback
- **Loading states** for async operations
- **Retry mechanisms** for failed requests

## ♿ Accessibility

### WCAG 2.2 Compliance

- **Keyboard Navigation**: All interactive elements are keyboard accessible
- **Screen Reader Support**: Proper ARIA labels and roles
- **Focus Management**: Clear focus indicators and focus trapping
- **Color Contrast**: Minimum 4.5:1 contrast ratio for text
- **Semantic HTML**: Proper use of semantic elements

### Testing Tools

- **axe-core**: Automated accessibility testing
- **Lighthouse**: Performance and accessibility audits
- **Manual Testing**: Screen reader and keyboard testing

## 📱 Responsive Design

### Breakpoints

- **Mobile**: `< 640px`
- **Tablet**: `640px - 1024px`
- **Desktop**: `> 1024px`

### Mobile Optimizations

- **Touch Targets**: Minimum 44px touch targets
- **Gesture Support**: Swipe and pinch gestures where appropriate
- **Performance**: Optimized for mobile networks and devices

## 🎨 Styling Approach

### CSS-in-JS Alternative

We use **CSS Modules** and **CSS Custom Properties** for styling:

- **Design Tokens**: Centralized CSS custom properties
- **Component Styles**: Scoped CSS files per component
- **Utility Classes**: Reusable utility classes in base.css
- **Responsive**: Mobile-first media queries

### CSS Architecture

1. **Design Tokens**: Global CSS custom properties
2. **Base Styles**: Reset, typography, utilities
3. **Component Styles**: Scoped component styles
4. **Application Styles**: Layout and page-specific styles

## 🧪 Testing

### Unit Testing

```bash
npm run test
```

- **Jest**: Test framework
- **React Testing Library**: Component testing utilities
- **Accessibility Testing**: axe-core integration

### Visual Testing

- **Storybook**: Component documentation and testing
- **Visual Regression**: Automated screenshot testing

## 🚀 Deployment

### Build Process

```bash
npm run build
```

- **Vite**: Fast build tool with HMR
- **Tree Shaking**: Automatic dead code elimination
- **Minification**: CSS and JavaScript optimization
- **Source Maps**: Production debugging support

### Production Considerations

- **Caching**: Proper cache headers for static assets
- **Compression**: Gzip/Brotli compression enabled
- **Security**: CSP headers and security best practices
- **Performance**: Image optimization and lazy loading

## 📚 Documentation

- **Component API**: TypeScript interfaces and props
- **Usage Examples**: Code examples in component files
- **Accessibility Guidelines**: ARIA patterns and keyboard support
- **Design Tokens**: Complete token reference

## 🤝 Contributing

1. **Fork the repository**
2. **Create a feature branch**
3. **Follow the design system patterns**
4. **Add tests for new components**
5. **Update documentation**
6. **Submit a pull request**

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](../../LICENSE) file for details.

## 🔗 Related Projects

- **Backend API**: [engine/](../engine/) - FastAPI backend
- **Documentation**: [wiki/](../../wiki/) - Project documentation
- **Infrastructure**: [infra/](../../infra/) - Deployment and infrastructure

---

**Built with ❤️ using React, TypeScript, and modern web standards**