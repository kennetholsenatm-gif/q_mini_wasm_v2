/**
 * Performance Optimization Utilities
 * Includes lazy loading, virtualization, memoization, and other performance enhancements
 */

import { useState, useEffect, useRef, useCallback, useMemo } from 'react';

/**
 * Lazy loading hook for images and other resources
 */
export const useLazyLoading = <T>(
  loadFunction: () => Promise<T>,
  dependencies: any[] = []
): {
  data: T | null;
  loading: boolean;
  error: Error | null;
  retry: () => void;
} => {
  const [data, setData] = useState<T | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<Error | null>(null);
  const isMountedRef = useRef(true);

  const loadData = useCallback(async () => {
    if (!isMountedRef.current) return;

    setLoading(true);
    setError(null);

    try {
      const result = await loadFunction();
      if (isMountedRef.current) {
        setData(result);
      }
    } catch (err) {
      if (isMountedRef.current) {
        setError(err instanceof Error ? err : new Error('Failed to load data'));
      }
    } finally {
      if (isMountedRef.current) {
        setLoading(false);
      }
    }
  }, [loadFunction]);

  useEffect(() => {
    loadData();
  }, dependencies);

  useEffect(() => {
    return () => {
      isMountedRef.current = false;
    };
  }, []);

  const retry = useCallback(() => {
    if (!loading) {
      loadData();
    }
  }, [loading, loadData]);

  return { data, loading, error, retry };
};

/**
 * Intersection Observer hook for lazy loading elements
 */
export const useIntersectionObserver = (
  options: IntersectionObserverInit = {}
): [React.RefCallback<Element>, boolean] => {
  const [isIntersecting, setIsIntersecting] = useState(false);
  const elementRef = useRef<Element | null>(null);

  useEffect(() => {
    const element = elementRef.current;
    if (!element) return;

    const observer = new IntersectionObserver(([entry]) => {
      setIsIntersecting(entry.isIntersecting);
    }, options);

    observer.observe(element);

    return () => {
      observer.disconnect();
    };
  }, [options]);

  const setElement = useCallback((element: Element | null) => {
    elementRef.current = element;
  }, []);

  return [setElement, isIntersecting];
};

/**
 * Debounce hook for delaying function calls
 */
export const useDebounce = <T>(value: T, delay: number): T => {
  const [debouncedValue, setDebouncedValue] = useState<T>(value);

  useEffect(() => {
    const handler = setTimeout(() => {
      setDebouncedValue(value);
    }, delay);

    return () => {
      clearTimeout(handler);
    };
  }, [value, delay]);

  return debouncedValue;
};

/**
 * Throttle hook for limiting function calls
 */
export const useThrottle = <T extends (...args: any[]) => any>(
  callback: T,
  delay: number
): T => {
  const lastRun = useRef(Date.now());

  return useCallback(
    ((...args: Parameters<T>) => {
      if (Date.now() - lastRun.current >= delay) {
        lastRun.current = Date.now();
        callback(...args);
      }
    }) as T,
    [callback, delay]
  );
};

/**
 * Memoized expensive calculations
 */
export const useExpensiveCalculation = <T>(
  calculation: () => T,
  dependencies: any[]
): T => {
  return useMemo(() => {
    console.log('Performing expensive calculation...');
    return calculation();
  }, dependencies);
};

/**
 * Virtualization hook for long lists
 */
export interface VirtualizationOptions {
  itemHeight: number;
  containerHeight: number;
  overscan?: number;
}

export const useVirtualization = (
  itemCount: number,
  options: VirtualizationOptions
): {
  visibleItems: { start: number; end: number };
  totalHeight: number;
  offsetY: number;
  onScroll: (event: React.UIEvent<HTMLDivElement>) => void;
} => {
  const { itemHeight, containerHeight, overscan = 5 } = options;
  const [offsetY, setOffsetY] = useState(0);

  const totalHeight = itemCount * itemHeight;
  const visibleCount = Math.ceil(containerHeight / itemHeight);
  
  const start = Math.max(0, Math.floor(offsetY / itemHeight) - overscan);
  const end = Math.min(itemCount, start + visibleCount + overscan * 2);

  const onScroll = useCallback((event: React.UIEvent<HTMLDivElement>) => {
    const target = event.currentTarget;
    setOffsetY(target.scrollTop);
  }, []);

  return {
    visibleItems: { start, end },
    totalHeight,
    offsetY,
    onScroll,
  };
};

/**
 * Resource preloading utilities
 */
export class ResourcePreloader {
  private static cache = new Map<string, any>();
  private static loadingPromises = new Map<string, Promise<any>>();

  /**
   * Preload an image
   */
  static preloadImage(src: string): Promise<HTMLImageElement> {
    if (this.cache.has(src)) {
      return Promise.resolve(this.cache.get(src));
    }

    if (this.loadingPromises.has(src)) {
      return this.loadingPromises.get(src);
    }

    const promise = new Promise<HTMLImageElement>((resolve, reject) => {
      const img = new Image();
      img.onload = () => {
        this.cache.set(src, img);
        resolve(img);
      };
      img.onerror = reject;
      img.src = src;
    });

    this.loadingPromises.set(src, promise);
    
    return promise.finally(() => {
      this.loadingPromises.delete(src);
    });
  }

  /**
   * Preload a script
   */
  static preloadScript(src: string): Promise<void> {
    if (this.cache.has(`script:${src}`)) {
      return Promise.resolve();
    }

    if (this.loadingPromises.has(`script:${src}`)) {
      return this.loadingPromises.get(`script:${src}`);
    }

    const promise = new Promise<void>((resolve, reject) => {
      const script = document.createElement('script');
      script.onload = () => {
        this.cache.set(`script:${src}`, true);
        resolve();
      };
      script.onerror = reject;
      script.src = src;
      document.head.appendChild(script);
    });

    this.loadingPromises.set(`script:${src}`, promise);
    
    return promise.finally(() => {
      this.loadingPromises.delete(`script:${src}`);
    });
  }

  /**
   * Preload a stylesheet
   */
  static preloadStylesheet(href: string): Promise<void> {
    if (this.cache.has(`css:${href}`)) {
      return Promise.resolve();
    }

    if (this.loadingPromises.has(`css:${href}`)) {
      return this.loadingPromises.get(`css:${href}`);
    }

    const promise = new Promise<void>((resolve, reject) => {
      const link = document.createElement('link');
      link.rel = 'stylesheet';
      link.onload = () => {
        this.cache.set(`css:${href}`, true);
        resolve();
      };
      link.onerror = reject;
      link.href = href;
      document.head.appendChild(link);
    });

    this.loadingPromises.set(`css:${href}`, promise);
    
    return promise.finally(() => {
      this.loadingPromises.delete(`css:${href}`);
    });
  }

  /**
   * Preload multiple resources
   */
  static preloadMultiple(resources: Array<{ type: 'image' | 'script' | 'css'; src: string }>): Promise<void[]> {
    const promises = resources.map(resource => {
      switch (resource.type) {
        case 'image':
          return this.preloadImage(resource.src);
        case 'script':
          return this.preloadScript(resource.src);
        case 'css':
          return this.preloadStylesheet(resource.src);
        default:
          return Promise.resolve();
      }
    });

    return Promise.all(promises);
  }

  /**
   * Clear cache
   */
  static clearCache(): void {
    this.cache.clear();
    this.loadingPromises.clear();
  }
}

/**
 * Performance monitoring utilities
 */
export class PerformanceMonitor {
  private static measurements = new Map<string, number[]>();

  /**
   * Measure function execution time
   */
  static measure<T>(name: string, fn: () => T): T {
    const start = performance.now();
    const result = fn();
    const end = performance.now();
    
    const duration = end - start;
    this.recordMeasurement(name, duration);
    
    console.log(`Performance: ${name} took ${duration.toFixed(2)}ms`);
    return result;
  }

  /**
   * Measure async function execution time
   */
  static async measureAsync<T>(name: string, fn: () => Promise<T>): Promise<T> {
    const start = performance.now();
    const result = await fn();
    const end = performance.now();
    
    const duration = end - start;
    this.recordMeasurement(name, duration);
    
    console.log(`Performance: ${name} took ${duration.toFixed(2)}ms`);
    return result;
  }

  /**
   * Record a performance measurement
   */
  private static recordMeasurement(name: string, duration: number): void {
    if (!this.measurements.has(name)) {
      this.measurements.set(name, []);
    }
    
    const measurements = this.measurements.get(name)!;
    measurements.push(duration);
    
    // Keep only last 100 measurements
    if (measurements.length > 100) {
      measurements.shift();
    }
  }

  /**
   * Get performance statistics
   */
  static getStats(name: string): {
    count: number;
    min: number;
    max: number;
    avg: number;
    p95: number;
  } | null {
    const measurements = this.measurements.get(name);
    if (!measurements || measurements.length === 0) {
      return null;
    }

    const sorted = [...measurements].sort((a, b) => a - b);
    const count = sorted.length;
    const min = sorted[0];
    const max = sorted[count - 1];
    const avg = sorted.reduce((a, b) => a + b, 0) / count;
    const p95 = sorted[Math.floor(count * 0.95)];

    return { count, min, max, avg, p95 };
  }

  /**
   * Clear all measurements
   */
  static clear(): void {
    this.measurements.clear();
  }
}

/**
 * Memory management utilities
 */
export class MemoryManager {
  private static cleanupCallbacks: (() => void)[] = [];

  /**
   * Register a cleanup callback
   */
  static registerCleanup(callback: () => void): void {
    this.cleanupCallbacks.push(callback);
  }

  /**
   * Run all cleanup callbacks
   */
  static cleanup(): void {
    this.cleanupCallbacks.forEach(callback => {
      try {
        callback();
      } catch (error) {
        console.error('Cleanup callback error:', error);
      }
    });
    this.cleanupCallbacks = [];
  }

  /**
   * Force garbage collection (if available)
   */
  static forceGC(): void {
    if (typeof window !== 'undefined' && (window as any).gc) {
      try {
        (window as any).gc();
      } catch (error) {
        console.warn('Garbage collection not available:', error);
      }
    }
  }

  /**
   * Monitor memory usage
   */
  static getMemoryUsage(): {
    used: number;
    total: number;
    percentage: number;
  } | null {
    if (typeof performance !== 'undefined' && 'memory' in performance) {
      const memory = (performance as any).memory;
      return {
        used: memory.usedJSHeapSize,
        total: memory.totalJSHeapSize,
        percentage: (memory.usedJSHeapSize / memory.totalJSHeapSize) * 100
      };
    }
    return null;
  }
}

/**
 * Bundle optimization utilities
 */
export class BundleOptimizer {
  /**
   * Dynamic import wrapper with error handling
   */
  static async loadModule<T>(importFn: () => Promise<{ default: T }>): Promise<T> {
    try {
      const module = await importFn();
      return module.default;
    } catch (error) {
      console.error('Failed to load module:', error);
      throw error;
    }
  }

  /**
   * Code splitting utility for routes
   */
  static createLazyRoute(importFn: () => Promise<any>) {
    return {
      lazy: () => this.loadModule(importFn),
      preload: () => importFn().catch(console.error)
    };
  }

  /**
   * Image optimization
   */
  static optimizeImage(src: string, options: {
    quality?: number;
    format?: 'webp' | 'jpeg' | 'png';
    maxWidth?: number;
    maxHeight?: number;
  } = {}): string {
    // This would integrate with an image optimization service
    // For now, return the original src
    return src;
  }
}

// Cleanup on page unload
if (typeof window !== 'undefined') {
  window.addEventListener('beforeunload', () => {
    MemoryManager.cleanup();
  });
}