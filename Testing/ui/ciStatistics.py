"""
Enhanced Confidence Interval Statistics Module

This module provides robust statistical functions for calculating confidence intervals
with proper diagnostics, especially designed for small sample sizes (n=3-5).

Author: LoRaChat Testing Framework
Date: 2025
"""

import numpy as np
import scipy.stats as stats
from typing import Dict, List, Optional, Tuple
import warnings


def test_normality(values: List[float], alpha: float = 0.05) -> Dict:
    """
    Test if data follows a normal distribution using Shapiro-Wilk test.

    Args:
        values: List of numeric values
        alpha: Significance level (default 0.05)

    Returns:
        Dictionary with:
            - 'test': Name of test used
            - 'statistic': Test statistic
            - 'p_value': p-value
            - 'is_normal': Whether data is consistent with normal distribution
            - 'warning': Warning message if sample size is too small
            - 'interpretation': Human-readable interpretation

    Note:
        Shapiro-Wilk test requires n >= 3. For n < 8, the test has low statistical
        power and should be interpreted cautiously.
    """
    n = len(values)

    if n < 3:
        return {
            'test': 'Shapiro-Wilk',
            'statistic': None,
            'p_value': None,
            'is_normal': None,
            'warning': 'Sample size too small for normality test (n < 3)',
            'interpretation': 'Cannot assess normality with n < 3. Visual inspection recommended.'
        }

    # Check if data has zero variance (all identical values)
    if np.std(values) == 0:
        return {
            'test': 'Shapiro-Wilk',
            'statistic': None,
            'p_value': None,
            'is_normal': True,  # Constant data is trivially "normal"
            'warning': 'All values are identical (zero variance). Normality test not applicable.',
            'interpretation': 'Data has no variance. Confidence interval is exact (no uncertainty).'
        }

    # Perform Shapiro-Wilk test
    statistic, p_value = stats.shapiro(values)
    is_normal = p_value > alpha

    # Generate interpretation
    if n < 8:
        warning = f'Low statistical power with n={n}. Normality test results should be interpreted cautiously.'
    else:
        warning = None

    if is_normal:
        interpretation = f'Data is consistent with normal distribution (p={p_value:.3f} > {alpha}). Parametric CI is appropriate.'
    else:
        interpretation = f'Data may not be normally distributed (p={p_value:.3f} < {alpha}). Consider bootstrap CI or report as limitation.'

    return {
        'test': 'Shapiro-Wilk',
        'statistic': statistic,
        'p_value': p_value,
        'is_normal': is_normal,
        'warning': warning,
        'interpretation': interpretation
    }


def calculate_ci_parametric(values: List[float], confidence_level: float = 0.95) -> Dict:
    """
    Calculate parametric confidence interval using Student's t-distribution.

    This is the standard approach for normally distributed data with small sample sizes.
    Valid for n >= 2, though wider CIs are expected for smaller n.

    Args:
        values: List of numeric values
        confidence_level: Confidence level (default 0.95 for 95% CI)

    Returns:
        Dictionary with mean, CI bounds, standard deviation, standard error, etc.
    """
    n = len(values)

    if n == 0:
        return None

    mean = np.mean(values)

    if n == 1:
        # Only one sample, no confidence interval possible
        return {
            'method': 'Parametric (t-distribution)',
            'mean': mean,
            'median': mean,
            'ci_lower': mean,
            'ci_upper': mean,
            'ci_width': 0,
            'std': 0,
            'se': 0,
            'n': n,
            'df': 0,
            'alpha': 1 - confidence_level,
            't_critical': None,
            'values': values,
            'warning': 'Only one sample - cannot calculate confidence interval'
        }

    # Calculate statistics
    std = np.std(values, ddof=1)  # Sample standard deviation
    se = std / np.sqrt(n)  # Standard error of the mean
    median = np.median(values)

    # Calculate confidence interval using t-distribution
    alpha = 1 - confidence_level
    df = n - 1
    t_critical = stats.t.ppf(1 - alpha/2, df=df)

    ci_lower = mean - t_critical * se
    ci_upper = mean + t_critical * se
    ci_width = ci_upper - ci_lower

    # Calculate relative error (useful for small samples)
    relative_error = (ci_width / 2) / mean * 100 if mean != 0 else None

    # Generate warnings for small samples
    warning = None
    if n < 3:
        warning = f'Very small sample (n={n}). CI will be very wide. Consider collecting more data.'
    elif n < 5:
        warning = f'Small sample (n={n}). CI is valid but wide. Individual data points should be reported.'

    return {
        'method': 'Parametric (t-distribution)',
        'mean': mean,
        'median': median,
        'ci_lower': ci_lower,
        'ci_upper': ci_upper,
        'ci_width': ci_width,
        'relative_error_pct': relative_error,
        'std': std,
        'se': se,
        'n': n,
        'df': df,
        'alpha': alpha,
        't_critical': t_critical,
        'confidence_level': confidence_level,
        'values': values,
        'warning': warning
    }


def calculate_ci_bootstrap(values: List[float], confidence_level: float = 0.95,
                          n_bootstrap: int = 10000, random_seed: Optional[int] = 42) -> Dict:
    """
    Calculate non-parametric confidence interval using bootstrap resampling.

    Bootstrap CI is distribution-free and can be more robust for small samples
    or non-normal data. Uses percentile method.

    Args:
        values: List of numeric values
        confidence_level: Confidence level (default 0.95)
        n_bootstrap: Number of bootstrap iterations (default 10000)
        random_seed: Random seed for reproducibility (default 42)

    Returns:
        Dictionary with mean, bootstrap CI bounds, and diagnostics
    """
    n = len(values)

    if n == 0:
        return None

    if n == 1:
        mean = np.mean(values)
        return {
            'method': 'Bootstrap (percentile)',
            'mean': mean,
            'median': mean,
            'ci_lower': mean,
            'ci_upper': mean,
            'ci_width': 0,
            'n': n,
            'n_bootstrap': n_bootstrap,
            'values': values,
            'warning': 'Only one sample - cannot calculate confidence interval'
        }

    # Set random seed for reproducibility
    if random_seed is not None:
        np.random.seed(random_seed)

    # Perform bootstrap
    bootstrap_means = []
    for _ in range(n_bootstrap):
        resample = np.random.choice(values, size=n, replace=True)
        bootstrap_means.append(np.mean(resample))

    # Calculate percentile confidence interval
    alpha = 1 - confidence_level
    ci_lower = np.percentile(bootstrap_means, alpha/2 * 100)
    ci_upper = np.percentile(bootstrap_means, (1 - alpha/2) * 100)

    mean = np.mean(values)
    median = np.median(values)
    ci_width = ci_upper - ci_lower

    # Calculate bootstrap standard error
    bootstrap_se = np.std(bootstrap_means)

    warning = None
    if n < 5:
        warning = f'Small sample (n={n}) may lead to unstable bootstrap estimates. Results should be interpreted cautiously.'

    return {
        'method': 'Bootstrap (percentile)',
        'mean': mean,
        'median': median,
        'ci_lower': ci_lower,
        'ci_upper': ci_upper,
        'ci_width': ci_width,
        'bootstrap_se': bootstrap_se,
        'n': n,
        'n_bootstrap': n_bootstrap,
        'confidence_level': confidence_level,
        'values': values,
        'warning': warning
    }


def calculate_ci_with_diagnostics(values: List[float],
                                   confidence_level: float = 0.95,
                                   test_normality_flag: bool = True,
                                   include_bootstrap: bool = False,
                                   bootstrap_iterations: int = 10000) -> Dict:
    """
    Calculate confidence intervals with full statistical diagnostics.

    This is the main function that provides comprehensive statistics including:
    - Parametric CI (t-distribution)
    - Optional bootstrap CI
    - Normality testing
    - Sample size warnings
    - Recommendations

    Args:
        values: List of numeric values
        confidence_level: Confidence level (default 0.95)
        test_normality_flag: Whether to perform normality test (default True)
        include_bootstrap: Whether to include bootstrap CI (default False)
        bootstrap_iterations: Number of bootstrap iterations if enabled

    Returns:
        Comprehensive dictionary with all statistics and diagnostics
    """
    if len(values) == 0:
        return {
            'error': 'No data provided',
            'n': 0
        }

    n = len(values)

    # Calculate parametric CI (always included)
    parametric_ci = calculate_ci_parametric(values, confidence_level)

    # Test normality if requested and sample size allows
    normality = None
    if test_normality_flag and n >= 3:
        normality = test_normality(values)

    # Calculate bootstrap CI if requested
    bootstrap_ci = None
    if include_bootstrap:
        bootstrap_ci = calculate_ci_bootstrap(values, confidence_level, bootstrap_iterations)

    # Generate recommendations
    recommendations = []

    if n == 1:
        recommendations.append("Only one data point - collect more replicates to enable statistical inference")
    elif n < 3:
        recommendations.append("Very small sample (n<3) - confidence intervals are not meaningful")
        recommendations.append("Report individual values and collect more replicates if possible")
    elif n < 5:
        recommendations.append("Small sample (n<5) - report individual data points alongside mean and CI")
        recommendations.append("Wide CIs are expected and appropriate for this sample size")
        recommendations.append("Consider showing box plots or violin plots to display all data")
    elif n < 10:
        recommendations.append("Moderate sample size - parametric CI is valid if data is approximately normal")
        recommendations.append("Consider reporting median alongside mean for robustness")

    if normality and not normality['is_normal'] and n >= 3:
        recommendations.append("Data may not be normally distributed - consider bootstrap CI or non-parametric methods")

    if normality and normality['warning']:
        recommendations.append(normality['warning'])

    # Calculate additional descriptive statistics
    descriptive_stats = {
        'min': np.min(values),
        'max': np.max(values),
        'range': np.max(values) - np.min(values),
        'q25': np.percentile(values, 25) if n > 1 else values[0],
        'q75': np.percentile(values, 75) if n > 1 else values[0],
        'iqr': np.percentile(values, 75) - np.percentile(values, 25) if n > 1 else 0,
        'cv': (parametric_ci['std'] / parametric_ci['mean'] * 100) if parametric_ci['mean'] != 0 and n > 1 else None  # Coefficient of variation
    }

    return {
        'parametric_ci': parametric_ci,
        'bootstrap_ci': bootstrap_ci,
        'normality': normality,
        'descriptive_stats': descriptive_stats,
        'recommendations': recommendations,
        'n': n,
        'confidence_level': confidence_level
    }


def format_ci_for_publication(ci_result: Dict, metric_name: str = "Value",
                              units: str = "%", decimal_places: int = 2) -> Dict[str, str]:
    """
    Format confidence interval results for publication.

    Args:
        ci_result: Result from calculate_ci_with_diagnostics or calculate_ci_parametric
        metric_name: Name of the metric being reported
        units: Units for the metric (default "%")
        decimal_places: Number of decimal places for rounding

    Returns:
        Dictionary with various formatted strings for different contexts
    """
    # Handle both full diagnostics and simple parametric CI
    if 'parametric_ci' in ci_result:
        ci = ci_result['parametric_ci']
    else:
        ci = ci_result

    mean = ci['mean']
    std = ci['std']
    ci_lower = ci['ci_lower']
    ci_upper = ci['ci_upper']
    n = ci['n']

    # Format with specified decimal places
    fmt = f"{{:.{decimal_places}f}}"

    return {
        'inline': f"{fmt.format(mean)}{units} (95% CI: {fmt.format(ci_lower)}-{fmt.format(ci_upper)}{units}, n={n})",
        'inline_compact': f"{fmt.format(mean)}{units} [{fmt.format(ci_lower)}, {fmt.format(ci_upper)}]",
        'mean_sd': f"{fmt.format(mean)} ± {fmt.format(std)}{units}",
        'mean_ci': f"{fmt.format(mean)}{units} (95% CI [{fmt.format(ci_lower)}, {fmt.format(ci_upper)}])",
        'table_mean': f"{fmt.format(mean)}",
        'table_sd': f"{fmt.format(std)}",
        'table_ci': f"[{fmt.format(ci_lower)}, {fmt.format(ci_upper)}]",
        'table_n': f"{n}",
        'latex_pm': f"${fmt.format(mean)} \\pm {fmt.format(std)}$",
        'caption': f"{metric_name} = {fmt.format(mean)}{units} (95% CI: {fmt.format(ci_lower)}-{fmt.format(ci_upper)}{units}, n={n})"
    }


def compare_with_theoretical(experimental_values: List[float],
                            theoretical_value: float,
                            confidence_level: float = 0.95) -> Dict:
    """
    Compare experimental measurements against a theoretical/expected value.

    Performs a one-sample t-test to determine if experimental values significantly
    differ from the theoretical prediction.

    Args:
        experimental_values: List of experimental measurements
        theoretical_value: Theoretical/expected value to compare against
        confidence_level: Confidence level for test (default 0.95)

    Returns:
        Dictionary with test results and interpretation
    """
    n = len(experimental_values)

    if n < 2:
        return {
            'error': 'Need at least 2 samples for statistical comparison',
            'n': n
        }

    # Perform one-sample t-test
    t_statistic, p_value = stats.ttest_1samp(experimental_values, theoretical_value)

    mean_exp = np.mean(experimental_values)
    std_exp = np.std(experimental_values, ddof=1)

    # Calculate effect size (Cohen's d)
    cohens_d = (mean_exp - theoretical_value) / std_exp if std_exp > 0 else None

    # Determine significance
    alpha = 1 - confidence_level
    is_significant = p_value < alpha

    # Calculate CI for the difference
    se = std_exp / np.sqrt(n)
    t_critical = stats.t.ppf(1 - alpha/2, df=n-1)
    ci_lower = (mean_exp - theoretical_value) - t_critical * se
    ci_upper = (mean_exp - theoretical_value) + t_critical * se

    # Interpretation
    if is_significant:
        interpretation = f"Experimental mean ({mean_exp:.3f}) differs significantly from theoretical value ({theoretical_value:.3f}), p={p_value:.3f}"
    else:
        interpretation = f"Experimental mean ({mean_exp:.3f}) is consistent with theoretical value ({theoretical_value:.3f}), p={p_value:.3f}"

    return {
        'test': 'One-sample t-test',
        'experimental_mean': mean_exp,
        'theoretical_value': theoretical_value,
        'difference': mean_exp - theoretical_value,
        'difference_ci_lower': ci_lower,
        'difference_ci_upper': ci_upper,
        't_statistic': t_statistic,
        'p_value': p_value,
        'alpha': alpha,
        'is_significant': is_significant,
        'cohens_d': cohens_d,
        'n': n,
        'interpretation': interpretation
    }
