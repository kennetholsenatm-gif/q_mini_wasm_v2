import React, { forwardRef, useEffect, useRef } from 'react';
import ReactDOM from 'react-dom';
import './Modal.css';

export interface ModalProps extends React.HTMLAttributes<HTMLDivElement> {
  isOpen: boolean;
  onClose: () => void;
  title?: string;
  size?: 'sm' | 'md' | 'lg' | 'xl' | 'full';
  closeOnOverlayClick?: boolean;
  closeOnEscape?: boolean;
  showCloseButton?: boolean;
}

export const Modal = forwardRef<HTMLDivElement, ModalProps>(
  (
    {
      isOpen,
      onClose,
      title,
      size = 'md',
      closeOnOverlayClick = true,
      closeOnEscape = true,
      showCloseButton = true,
      children,
      className = '',
      ...props
    },
    ref
  ) => {
    const modalRef = useRef<HTMLDivElement>(null);
    const previousActiveElement = useRef<HTMLElement | null>(null);

    // Handle focus management and keyboard events
    useEffect(() => {
      if (!isOpen) return;

      // Store the previously focused element
      previousActiveElement.current = document.activeElement as HTMLElement;

      // Focus the modal
      const modalElement = modalRef.current;
      if (modalElement) {
        modalElement.focus();
      }

      // Handle keyboard events
      const handleKeyDown = (e: KeyboardEvent) => {
        if (e.key === 'Escape' && closeOnEscape) {
          onClose();
        } else if (e.key === 'Tab') {
          // Trap focus within the modal
          trapFocus(e);
        }
      };

      // Handle clicks outside the modal
      const handleOverlayClick = (e: MouseEvent) => {
        if (modalElement && !modalElement.contains(e.target as Node) && closeOnOverlayClick) {
          onClose();
        }
      };

      document.addEventListener('keydown', handleKeyDown);
      if (closeOnOverlayClick) {
        document.addEventListener('mousedown', handleOverlayClick);
      }

      return () => {
        document.removeEventListener('keydown', handleKeyDown);
        document.removeEventListener('mousedown', handleOverlayClick);
        
        // Restore focus to the previously focused element
        if (previousActiveElement.current) {
          previousActiveElement.current.focus();
        }
      };
    }, [isOpen, onClose, closeOnEscape, closeOnOverlayClick]);

    // Focus trap function
    const trapFocus = (e: KeyboardEvent) => {
      const modalElement = modalRef.current;
      if (!modalElement) return;

      const focusableElements = modalElement.querySelectorAll(
        'button, [href], input, select, textarea, [tabindex]:not([tabindex="-1"])'
      ) as NodeListOf<HTMLElement>;
      
      const firstElement = focusableElements[0];
      const lastElement = focusableElements[focusableElements.length - 1];

      if (e.key === 'Tab') {
        if (e.shiftKey) {
          // Shift + Tab
          if (document.activeElement === firstElement) {
            e.preventDefault();
            lastElement?.focus();
          }
        } else {
          // Tab
          if (document.activeElement === lastElement) {
            e.preventDefault();
            firstElement?.focus();
          }
        }
      }
    };

    // Don't render if not open
    if (!isOpen) return null;

    const modalContent = (
      <div className="modal-overlay" role="dialog" aria-modal="true" aria-labelledby={title ? 'modal-title' : undefined}>
        <div
          ref={(node) => {
            modalRef.current = node;
            if (typeof ref === 'function') {
              ref(node);
            } else if (ref) {
              (ref as React.MutableRefObject<HTMLDivElement | null>).current = node;
            }
          }}
          className={`modal modal-${size} ${className}`}
          tabIndex={-1}
          {...props}
        >
          {(title || showCloseButton) && (
            <div className="modal-header">
              {title && (
                <h2 id="modal-title" className="modal-title">
                  {title}
                </h2>
              )}
              {showCloseButton && (
                <button
                  type="button"
                  className="modal-close"
                  onClick={onClose}
                  aria-label="Close modal"
                >
                  <svg
                    className="modal-close-icon"
                    viewBox="0 0 24 24"
                    fill="none"
                    aria-hidden="true"
                  >
                    <path
                      d="M6 6L18 18M18 6L6 18"
                      stroke="currentColor"
                      strokeWidth="2"
                      strokeLinecap="round"
                      strokeLinejoin="round"
                    />
                  </svg>
                </button>
              )}
            </div>
          )}
          
          <div className="modal-body">
            {children}
          </div>
        </div>
      </div>
    );

    // Render to portal
    return ReactDOM.createPortal(modalContent, document.body);
  }
);

Modal.displayName = 'Modal';