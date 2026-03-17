import React, { InputHTMLAttributes, forwardRef, useState } from 'react';
import './Input.css';

export interface InputProps extends InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  error?: string;
  helperText?: string;
  leftIcon?: React.ReactNode;
  rightIcon?: React.ReactNode;
  fullWidth?: boolean;
  size?: 'sm' | 'md' | 'lg';
}

export const Input = forwardRef<HTMLInputElement, InputProps>(
  (
    {
      label,
      error,
      helperText,
      leftIcon,
      rightIcon,
      fullWidth = false,
      size = 'md',
      className = '',
      id,
      ...props
    },
    ref
  ) => {
    const [isFocused, setIsFocused] = useState(false);
    const [isDirty, setIsDirty] = useState(false);
    
    const inputId = id || `input-${Math.random().toString(36).substr(2, 9)}`;
    const errorId = `${inputId}-error`;
    const helperId = `${inputId}-helper`;

    const baseClasses = [
      'input',
      `input-${size}`,
      fullWidth && 'input-full-width',
      isFocused && 'input-focused',
      error && 'input-error',
      isDirty && 'input-dirty',
      className
    ]
      .filter(Boolean)
      .join(' ');

    const handleFocus = (e: React.FocusEvent<HTMLInputElement>) => {
      setIsFocused(true);
      props.onFocus?.(e);
    };

    const handleBlur = (e: React.FocusEvent<HTMLInputElement>) => {
      setIsFocused(false);
      setIsDirty(true);
      props.onBlur?.(e);
    };

    return (
      <div className={`input-wrapper ${fullWidth ? 'input-wrapper-full-width' : ''}`}>
        {label && (
          <label htmlFor={inputId} className="input-label">
            {label}
          </label>
        )}
        
        <div className="input-container">
          {leftIcon && (
            <span className="input-icon input-icon-left" aria-hidden="true">
              {leftIcon}
            </span>
          )}
          
          <input
            ref={ref}
            id={inputId}
            className={baseClasses}
            onFocus={handleFocus}
            onBlur={handleBlur}
            aria-invalid={!!error}
            aria-describedby={[
              error ? errorId : '',
              helperText ? helperId : '',
              props['aria-describedby'] || ''
            ]
              .filter(Boolean)
              .join(' ')}
            {...props}
          />
          
          {rightIcon && (
            <span className="input-icon input-icon-right" aria-hidden="true">
              {rightIcon}
            </span>
          )}
        </div>
        
        {(error || helperText) && (
          <div className="input-hint">
            {error ? (
              <span id={errorId} className="input-error-text" role="alert">
                {error}
              </span>
            ) : (
              <span id={helperId} className="input-helper-text">
                {helperText}
              </span>
            )}
          </div>
        )}
      </div>
    );
  }
);

Input.displayName = 'Input';