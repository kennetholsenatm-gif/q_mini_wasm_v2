/**
 * Security Headers Configuration
 * Implements Content Security Policy and other security headers
 */

export interface SecurityHeaders {
  'Content-Security-Policy': string;
  'X-Frame-Options': string;
  'X-Content-Type-Options': string;
  'Referrer-Policy': string;
  'Permissions-Policy': string;
  'Strict-Transport-Security': string;
  'X-XSS-Protection': string;
}

/**
 * Content Security Policy configuration
 * Restricts sources for various content types to prevent XSS and other attacks
 */
export const createCSP = (environment: string = 'production'): string => {
  const isDevelopment = environment === 'development';
  
  // Base CSP directives
  const directives = {
    'default-src': ["'self'"],
    'script-src': isDevelopment 
      ? ["'self'", "'unsafe-inline'", "'unsafe-eval'", 'localhost:*', '127.0.0.1:*']
      : ["'self'"],
    'style-src': ["'self'", "'unsafe-inline'", 'https://fonts.googleapis.com'],
    'font-src': ["'self'", 'https://fonts.gstatic.com', 'data:'],
    'img-src': ["'self'", 'data:', 'https:', 'blob:'],
    'connect-src': isDevelopment
      ? ["'self'", 'ws:', 'wss:', 'http://localhost:*', 'https://localhost:*', 'http://127.0.0.1:*', 'https://127.0.0.1:*']
      : ["'self'", 'wss:', 'https:'],
    'media-src': ["'self'", 'data:', 'https:'],
    'object-src': ["'none'"],
    'base-uri': ["'self'"],
    'form-action': ["'self'"],
    'frame-ancestors': ["'none'"],
    'upgrade-insecure-requests': isDevelopment ? undefined : undefined,
  };

  // Build CSP string
  return Object.entries(directives)
    .filter(([_, value]) => value !== undefined)
    .map(([directive, sources]) => {
      if (sources === undefined) return directive;
      return `${directive} ${Array.isArray(sources) ? sources.join(' ') : sources}`;
    })
    .join('; ');
};

/**
 * Get all security headers for the application
 */
export const getSecurityHeaders = (environment: string = 'production'): SecurityHeaders => {
  const isDevelopment = environment === 'development';
  
  return {
    'Content-Security-Policy': createCSP(environment),
    'X-Frame-Options': 'DENY',
    'X-Content-Type-Options': 'nosniff',
    'Referrer-Policy': 'strict-origin-when-cross-origin',
    'Permissions-Policy': 'camera=(), microphone=(), geolocation=(), payment=(), usb=(), magnetometer=(), gyroscope=(), speaker=()',
    'Strict-Transport-Security': isDevelopment ? 'max-age=31536000; includeSubDomains' : 'max-age=31536000; includeSubDomains; preload',
    'X-XSS-Protection': '1; mode=block',
  };
};

/**
 * Security utilities for input validation and sanitization
 */
export class SecurityUtils {
  /**
   * Sanitize user input to prevent XSS attacks
   */
  static sanitizeInput(input: string): string {
    if (typeof input !== 'string') return '';
    
    return input
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#x27;')
      .replace(/\//g, '&#x2F;');
  }

  /**
   * Validate email format
   */
  static validateEmail(email: string): boolean {
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    return emailRegex.test(email);
  }

  /**
   * Validate URL format
   */
  static validateUrl(url: string): boolean {
    try {
      const urlObj = new URL(url);
      return ['http:', 'https:'].includes(urlObj.protocol);
    } catch {
      return false;
    }
  }

  /**
   * Check for SQL injection patterns
   */
  static isSqlInjectionAttempt(input: string): boolean {
    const sqlPatterns = [
      /(\bselect\b|\binsert\b|\bupdate\b|\bdelete\b|\bdrop\b|\bunion\b)/i,
      /('|(\\')|(;)|(--)|(\||(\%27)|(\%22)))/i,
      /(\bor\b|\band\b).*(=|>|<|LIKE)/i,
    ];

    return sqlPatterns.some(pattern => pattern.test(input));
  }

  /**
   * Check for XSS patterns.
   * Script/iframe/object end tags use [^>]* before '>' to match variants like </script >, </script\t\n bar>, etc.
   */
  static isXssAttempt(input: string): boolean {
    const xssPatterns = [
      /<script\b[^<]*(?:(?!<\/script[^>]*>)<[^<]*)*<\/script[^>]*>/gi,
      /javascript:/gi,
      /on\w+\s*=/gi,
      /<iframe\b[^<]*(?:(?!<\/iframe[^>]*>)<[^<]*)*<\/iframe[^>]*>/gi,
      /<object\b[^<]*(?:(?!<\/object[^>]*>)<[^<]*)*<\/object[^>]*>/gi,
    ];

    return xssPatterns.some(pattern => pattern.test(input));
  }

  /**
   * Generate a secure random token
   */
  static generateSecureToken(length: number = 32): string {
    const charset = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    
    if (typeof crypto !== 'undefined' && crypto.getRandomValues) {
      const array = new Uint8Array(length);
      crypto.getRandomValues(array);
      for (let i = 0; i < length; i++) {
        result += charset[array[i] % charset.length];
      }
    } else {
      // Fallback for environments without crypto
      for (let i = 0; i < length; i++) {
        result += charset[Math.floor(Math.random() * charset.length)];
      }
    }
    
    return result;
  }

  /**
   * Hash a string using Web Crypto API
   */
  static async hashString(input: string): Promise<string> {
    if (typeof crypto !== 'undefined' && crypto.subtle) {
      const encoder = new TextEncoder();
      const data = encoder.encode(input);
      const hashBuffer = await crypto.subtle.digest('SHA-256', data);
      const hashArray = Array.from(new Uint8Array(hashBuffer));
      return hashArray.map(b => b.toString(16).padStart(2, '0')).join('');
    }
    
    // Fallback hash function
    let hash = 0;
    for (let i = 0; i < input.length; i++) {
      const char = input.charCodeAt(i);
      hash = ((hash << 5) - hash) + char;
      hash = hash & hash; // Convert to 32-bit integer
    }
    return Math.abs(hash).toString(16);
  }

  /**
   * Validate password strength
   */
  static validatePassword(password: string): {
    isValid: boolean;
    errors: string[];
  } {
    const errors: string[] = [];
    
    if (password.length < 8) {
      errors.push('Password must be at least 8 characters long');
    }
    
    if (!/[a-z]/.test(password)) {
      errors.push('Password must contain at least one lowercase letter');
    }
    
    if (!/[A-Z]/.test(password)) {
      errors.push('Password must contain at least one uppercase letter');
    }
    
    if (!/[0-9]/.test(password)) {
      errors.push('Password must contain at least one number');
    }
    
    if (!/[!@#$%^&*(),.?":{}|<>]/.test(password)) {
      errors.push('Password must contain at least one special character');
    }
    
    return {
      isValid: errors.length === 0,
      errors
    };
  }

  /**
   * Rate limiting utility
   */
  static createRateLimiter(maxAttempts: number, windowMs: number) {
    const attempts = new Map<string, { count: number; resetTime: number }>();

    return {
      isAllowed: (key: string): boolean => {
        const now = Date.now();
        const attempt = attempts.get(key);

        if (!attempt) {
          attempts.set(key, { count: 1, resetTime: now + windowMs });
          return true;
        }

        if (now > attempt.resetTime) {
          attempts.set(key, { count: 1, resetTime: now + windowMs });
          return true;
        }

        if (attempt.count >= maxAttempts) {
          return false;
        }

        attempt.count++;
        return true;
      },

      reset: (key: string) => {
        attempts.delete(key);
      },

      getAttempts: (key: string): number => {
        const attempt = attempts.get(key);
        return attempt ? attempt.count : 0;
      }
    };
  }
}

/**
 * CSRF protection utilities
 */
export class CSRFProtection {
  private static tokenKey = 'csrf_token';
  private static token: string | null = null;

  /**
   * Generate or retrieve CSRF token
   */
  static getToken(): string {
    if (!this.token) {
      this.token = localStorage.getItem(this.tokenKey) || SecurityUtils.generateSecureToken();
      localStorage.setItem(this.tokenKey, this.token);
    }
    return this.token;
  }

  /**
   * Clear CSRF token
   */
  static clearToken(): void {
    this.token = null;
    localStorage.removeItem(this.tokenKey);
  }

  /**
   * Add CSRF token to request headers
   */
  static addCSRFToken(headers: Record<string, string> = {}): Record<string, string> {
    return {
      ...headers,
      'X-CSRF-Token': this.getToken()
    };
  }
}

/**
 * Secure storage utilities
 */
export class SecureStorage {
  private static encryptionKey: CryptoKey | null = null;
  private static isCryptoAvailable = typeof crypto !== 'undefined' && crypto.subtle;

  /**
   * Initialize encryption key
   */
  static async initializeKey(): Promise<void> {
    if (!this.isCryptoAvailable) return;

    try {
      const keyMaterial = await crypto.subtle.importKey(
        'raw',
        new TextEncoder().encode('qminiwasm-secure-key'),
        { name: 'PBKDF2' },
        false,
        ['deriveKey']
      );

      this.encryptionKey = await crypto.subtle.deriveKey(
        {
          name: 'PBKDF2',
          salt: new TextEncoder().encode('qminiwasm-salt'),
          iterations: 100000,
          hash: 'SHA-256'
        },
        keyMaterial,
        { name: 'AES-GCM', length: 256 },
        false,
        ['encrypt', 'decrypt']
      );
    } catch (error) {
      console.warn('Failed to initialize encryption key:', error);
    }
  }

  /**
   * Store encrypted data in localStorage
   */
  static async setItem(key: string, value: string): Promise<void> {
    if (!this.isCryptoAvailable || !this.encryptionKey) {
      // Fallback to regular localStorage
      localStorage.setItem(key, value);
      return;
    }

    try {
      const encoder = new TextEncoder();
      const data = encoder.encode(value);
      const iv = crypto.getRandomValues(new Uint8Array(12));
      
      const encrypted = await crypto.subtle.encrypt(
        { name: 'AES-GCM', iv: iv },
        this.encryptionKey,
        data
      );

      const combined = new Uint8Array(iv.byteLength + encrypted.byteLength);
      combined.set(iv, 0);
      combined.set(new Uint8Array(encrypted), iv.byteLength);

      localStorage.setItem(key, btoa(String.fromCharCode(...combined)));
    } catch (error) {
      console.error('Failed to encrypt data:', error);
      localStorage.setItem(key, value);
    }
  }

  /**
   * Retrieve and decrypt data from localStorage
   */
  static async getItem(key: string): Promise<string | null> {
    const stored = localStorage.getItem(key);
    if (!stored) return null;

    if (!this.isCryptoAvailable || !this.encryptionKey) {
      return stored;
    }

    try {
      const combined = Uint8Array.from(atob(stored), c => c.charCodeAt(0));
      const iv = combined.slice(0, 12);
      const encrypted = combined.slice(12);

      const decrypted = await crypto.subtle.decrypt(
        { name: 'AES-GCM', iv: iv },
        this.encryptionKey,
        encrypted
      );

      return new TextDecoder().decode(decrypted);
    } catch (error) {
      console.error('Failed to decrypt data:', error);
      return stored;
    }
  }

  /**
   * Remove encrypted item
   */
  static removeItem(key: string): void {
    localStorage.removeItem(key);
  }
}

// Initialize encryption key on module load
if (typeof window !== 'undefined') {
  SecureStorage.initializeKey();
}