import React, { forwardRef } from 'react';
import './Card.css';

export interface CardProps extends React.HTMLAttributes<HTMLDivElement> {
  variant?: 'default' | 'elevated' | 'outlined';
  padding?: 'sm' | 'md' | 'lg' | 'none';
  rounded?: 'sm' | 'md' | 'lg' | 'full';
  shadow?: 'none' | 'sm' | 'md' | 'lg';
}

export const Card = forwardRef<HTMLDivElement, CardProps>(
  (
    {
      children,
      className = '',
      variant = 'default',
      padding = 'md',
      rounded = 'md',
      shadow = 'sm',
      ...props
    },
    ref
  ) => {
    const baseClasses = [
      'card',
      `card-${variant}`,
      `card-padding-${padding}`,
      `card-rounded-${rounded}`,
      `card-shadow-${shadow}`,
      className
    ]
      .filter(Boolean)
      .join(' ');

    return (
      <div
        ref={ref}
        className={baseClasses}
        {...props}
      >
        {children}
      </div>
    );
  }
);

Card.displayName = 'Card';

export interface CardHeaderProps extends React.HTMLAttributes<HTMLDivElement> {
  border?: boolean;
}

export const CardHeader = forwardRef<HTMLDivElement, CardHeaderProps>(
  ({ children, className = '', border = false, ...props }, ref) => {
    const baseClasses = [
      'card-header',
      border && 'card-header-border',
      className
    ]
      .filter(Boolean)
      .join(' ');

    return (
      <div ref={ref} className={baseClasses} {...props}>
        {children}
      </div>
    );
  }
);

CardHeader.displayName = 'CardHeader';

export interface CardContentProps extends React.HTMLAttributes<HTMLDivElement> {}

export const CardContent = forwardRef<HTMLDivElement, CardContentProps>(
  ({ children, className = '', ...props }, ref) => {
    return (
      <div ref={ref} className={`card-content ${className}`} {...props}>
        {children}
      </div>
    );
  }
);

CardContent.displayName = 'CardContent';

export interface CardFooterProps extends React.HTMLAttributes<HTMLDivElement> {
  border?: boolean;
}

export const CardFooter = forwardRef<HTMLDivElement, CardFooterProps>(
  ({ children, className = '', border = false, ...props }, ref) => {
    const baseClasses = [
      'card-footer',
      border && 'card-footer-border',
      className
    ]
      .filter(Boolean)
      .join(' ');

    return (
      <div ref={ref} className={baseClasses} {...props}>
        {children}
      </div>
    );
  }
);

CardFooter.displayName = 'CardFooter';