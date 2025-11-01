# Confidence Intervals Guide for LoRaChat Testing Framework

**Complete Guide to Calculating, Interpreting, and Reporting Confidence Intervals**

---

## Table of Contents

1. [Introduction](#introduction)
2. [What is a Confidence Interval?](#what-is-a-confidence-interval)
3. [When to Use Different CI Methods](#when-to-use-different-ci-methods)
4. [Implementation in LoRaChat Framework](#implementation-in-lorachat-framework)
5. [Special Considerations for Small Sample Sizes](#special-considerations-for-small-sample-sizes)
6. [How to Report CIs in Scientific Papers](#how-to-report-cis-in-scientific-papers)
7. [Common Mistakes to Avoid](#common-mistakes-to-avoid)
8. [Examples and Best Practices](#examples-and-best-practices)
9. [References](#references)

---

## Introduction

This guide explains how to properly calculate, interpret, and report confidence intervals (CIs) for experimental data in the LoRaChat testing framework. It is specifically designed for scenarios with **small sample sizes (n=3-5)**, which are common in computationally expensive simulations and IoT experiments.

### Key Takeaways

✅ **Your current implementation is mathematically correct** - You're using Student's t-distribution, which is the gold standard for small samples.

✅ **Wide CIs are expected and appropriate** for small sample sizes - They correctly reflect uncertainty.

✅ **Report individual data points** alongside means and CIs when n < 5 for transparency.

---

## What is a Confidence Interval?

A **confidence interval (CI)** provides a range of plausible values for a population parameter (usually the mean) based on sample data.

### Interpretation

**95% CI [13.2, 18.4]** means:

> "If we repeated this experiment many times and calculated a 95% CI each time, approximately 95% of those intervals would contain the true population mean."

### What CIs Tell You

- **Precision**: Narrow CI = high precision, Wide CI = low precision
- **Uncertainty**: The CI width reflects both data variability and sample size
- **Statistical Significance**: If CIs of two groups don't overlap, they likely differ significantly

### What CIs Do NOT Tell You

❌ **NOT**: "There's a 95% probability the true mean is in this interval" (frequentist interpretation)
❌ **NOT**: "95% of the data falls within this interval" (that's a prediction interval)
❌ **NOT**: Non-overlapping CIs always mean significant difference (p-values are more precise)

---

## When to Use Different CI Methods

### 1. Parametric CI (Student's t-distribution) ✅ **Default Method**

**Use when:**
- n ≥ 2 (though n ≥ 3 is better)
- Data is approximately normally distributed (or n ≥ 10-15)
- You want the standard approach used in most scientific papers

**Formula:**
```
CI = mean ± t(α/2, n-1) × (SD / √n)
```

Where:
- `t(α/2, n-1)` = critical value from t-distribution (e.g., 2.776 for n=5 at 95% CI)
- `SD` = sample standard deviation
- `n` = sample size

**Implementation:**
```python
from ciStatistics import calculate_ci_parametric

result = calculate_ci_parametric(values, confidence_level=0.95)
print(f"Mean: {result['mean']:.2f}")
print(f"95% CI: [{result['ci_lower']:.2f}, {result['ci_upper']:.2f}]")
```

### 2. Bootstrap CI (Non-parametric)

**Use when:**
- Data is clearly non-normal (skewed, bimodal)
- n is very small (< 5) and normality is uncertain
- You want a sensitivity check against parametric CI

**Advantages:**
- No assumption of normality
- Works with small samples
- Distribution-free

**Limitations:**
- Can be unstable with n < 5
- Computationally intensive (10,000+ iterations)
- Less familiar to reviewers (requires explanation)

**Implementation:**
```python
from ciStatistics import calculate_ci_bootstrap

result = calculate_ci_bootstrap(values, confidence_level=0.95, n_bootstrap=10000)
print(f"Bootstrap 95% CI: [{result['ci_lower']:.2f}, {result['ci_upper']:.2f}]")
```

### 3. Comprehensive Analysis with Diagnostics ⭐ **Recommended**

**Use this for publication-ready analysis:**

```python
from ciStatistics import calculate_ci_with_diagnostics

result = calculate_ci_with_diagnostics(
    values,
    confidence_level=0.95,
    test_normality_flag=True,
    include_bootstrap=False  # Set True for sensitivity analysis
)

# Access results
print(result['parametric_ci'])
print(result['normality'])
print(result['recommendations'])
```

---

## Implementation in LoRaChat Framework

### Basic Usage

```python
import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), ".."))
from Testing.ui.ciStatistics import calculate_ci_with_diagnostics
from Testing.ui.exportResults import export_all_formats

# Your experimental data
experiment_data = [
    {'label': 'Config1', 'n': 5, 'metrics': {...}},
    {'label': 'Config2', 'n': 5, 'metrics': {...}}
]

# Export in all formats
output_files = export_all_formats(
    results=experiment_data,
    output_dir='./results',
    base_filename='experiment_2025_01',
    confidence_level=0.95,
    table_caption='LoRa Network Performance Metrics'
)

# Prints paths to generated files (.tex, .csv, .md, .txt)
```

### Integration with Existing Plot Scripts

The plotting functions in `Testing/ui/` have been updated to use the new CI calculation methods:

```python
from Testing.ui.drawExperimentComparisonWithTheoretical import (
    draw_experiment_comparison_with_theoretical
)

# This now includes normality testing and enhanced statistics
draw_experiment_comparison_with_theoretical(
    frame=my_frame,
    directory='./experiment_results',
    confidence_level=0.95
)
```

---

## Special Considerations for Small Sample Sizes

### Sample Size Guidelines

| n | Status | Recommendations |
|---|--------|-----------------|
| 1 | ❌ **Insufficient** | Cannot calculate CI. Report single value, collect more data. |
| 2 | ⚠️ **Very Limited** | CI is possible but extremely wide. t-critical ≈ 12.7 |
| 3-5 | ⚠️ **Small** | Valid but wide CIs. **Report individual data points**. Consider bootstrap. |
| 5-15 | ✅ **Adequate** | Parametric CI is reliable if data is approximately normal. |
| 15-30 | ✅ **Good** | t-distribution converges to normal. Robust results. |
| 30+ | ✅ **Large** | Can use normal approximation (z-score), but t-distribution still valid. |

### For n = 3-5 (Your Case)

**Do:**
✅ Use Student's t-distribution (already implemented)
✅ Report individual data points in tables/supplements (optional: enable scatter overlay in plots)
✅ Acknowledge wide CIs as appropriate given sample size
✅ Test normality (but note low power with small n)
✅ Consider bootstrap CI as sensitivity check
✅ Show box plots or violin plots to display all data (if needed)

**Don't:**
❌ Claim "high precision" with wide CIs
❌ Use z-scores (normal approximation) instead of t-distribution
❌ Hide uncertainty by only reporting means
❌ Over-interpret non-significant differences (low statistical power)
❌ Ignore outliers without investigation

### t-Critical Values for Small Samples

For 95% CI:

| n | df (n-1) | t-critical |
|---|----------|------------|
| 2 | 1 | 12.706 |
| 3 | 2 | 4.303 |
| 4 | 3 | 3.182 |
| 5 | 4 | 2.776 |
| 10 | 9 | 2.262 |
| 30 | 29 | 2.045 |
| ∞ | ∞ | 1.960 (z-score) |

**Notice:** As n increases, t-critical approaches 1.96 (the z-score for 95% CI).

---

## How to Report CIs in Scientific Papers

### In Text

**Good Examples:**

1. "The experimental overhead was **15.3% (95% CI: 13.8-16.8%, n=5)**"
2. "Packet loss averaged **3.2% (95% CI [2.1, 4.3], n=5)**"
3. "Mean latency was **245 ms (95% CI: 198-292 ms, n=4)**"

**Include:**
- Mean value
- CI bounds
- Confidence level (95% is standard)
- Sample size (n)

### In Tables

**LaTeX Example:**

```latex
\begin{table}[htbp]
\centering
\caption{Network Performance Metrics}
\label{tab:performance}
\begin{tabular}{lcccc}
\hline
Configuration & Mean (\%) & SD (\%) & 95\% CI (\%) & n \\
\hline
Reliable-Multi & 15.3 & 2.1 & [13.2, 18.4] & 5 \\
Unreliable-Multi & 8.7 & 1.5 & [7.1, 10.3] & 5 \\
\hline
\end{tabular}
\end{table}
```

**Generated automatically by:**
```python
from Testing.ui.exportResults import export_to_latex
export_to_latex(results, 'results.tex', 'Network Performance Metrics')
```

### In Figure Captions

**Example:**

> **Figure 3. Experimental overhead comparison across configurations.** Bars represent mean values with error bars indicating 95% confidence intervals calculated using Student's t-distribution (n=5 per configuration). Individual data points are overlaid as circles. Theoretical overhead (blue bars) is deterministic and has no error bars.

**Key elements:**
- What the bars/points represent
- What the error bars represent (95% CI)
- Method used (Student's t-distribution)
- Sample size
- Any special notes (theoretical values, etc.)

### In Methods Section

**Complete Example:**

> **Statistical Analysis.** Each experimental configuration was repeated 5 times. Results are reported as mean ± standard deviation (SD). Confidence intervals (95% CI) were calculated using Student's t-distribution with n-1 degrees of freedom, appropriate for small sample sizes [1]. The confidence interval was computed as:
>
> CI = mean ± t(α/2, n-1) × (SD / √n)
>
> where t(α/2, n-1) is the critical value from the t-distribution at significance level α = 0.05. Normality was assessed using the Shapiro-Wilk test [2]. Given the small sample sizes (n=5), individual data points are reported alongside summary statistics to provide transparency about the underlying data distribution. The wide confidence intervals appropriately reflect both measurement variability and statistical uncertainty due to limited sample size.

**Generated automatically by:**
```python
from Testing.ui.exportResults import generate_methods_section
methods_text = generate_methods_section(
    sample_sizes=[5, 5, 4, 5],
    confidence_level=0.95,
    includes_normality_test=True
)
```

### References for Methods Section

**Essential Citations:**

1. **Student's t-distribution:**
   Student (1908). "The probable error of a mean". *Biometrika*, 6(1), 1-25.

2. **Shapiro-Wilk normality test:**
   Shapiro, S.S., & Wilk, M.B. (1965). "An analysis of variance test for normality (complete samples)". *Biometrika*, 52(3-4), 591-611.

3. **Bootstrap methods (if used):**
   Efron, B., & Tibshirani, R.J. (1994). "An Introduction to the Bootstrap". CRC Press.

---

## Common Mistakes to Avoid

### ❌ Mistake 1: Using Standard Deviation as Error Bars

**Wrong:**
```python
plt.errorbar(x, means, yerr=std_devs)  # This shows ±1 SD, NOT CI!
```

**Correct:**
```python
# Show confidence intervals
errors = [ci_upper - mean for ci_upper, mean in zip(ci_uppers, means)]
plt.errorbar(x, means, yerr=errors, capsize=5)
```

**Why it matters:**
- SD shows data spread (about 68% of data within ±1 SD)
- CI shows uncertainty in the mean estimate
- For n=5, the 95% CI is about **2.78 × SE**, not 1 × SD

### ❌ Mistake 2: Confusing SE and SD

- **Standard Deviation (SD)**: Measures data variability
  - Formula: `SD = √(Σ(x - mean)² / (n-1))`
  - Interpretation: "Most data points fall within mean ± 1 SD"

- **Standard Error (SE)**: Measures uncertainty in the mean
  - Formula: `SE = SD / √n`
  - Interpretation: "The true population mean is likely near sample mean ± SE"

**Key Difference:**
- SD stays relatively constant as n increases
- SE decreases as n increases (more data → better estimate)

### ❌ Mistake 3: Using z-score for Small Samples

**Wrong:**
```python
# Using z=1.96 for 95% CI
ci = mean ± 1.96 × se  # Only valid for large n (n > 30)
```

**Correct:**
```python
# Using t-distribution
t_critical = stats.t.ppf(0.975, df=n-1)
ci = mean ± t_critical × se
```

**Impact for n=5:**
- z = 1.96 (incorrect)
- t = 2.776 (correct)
- Difference: **41% wider CI** when using t (as it should be!)

### ❌ Mistake 4: Not Reporting Sample Size

**Wrong:**
"Mean overhead was 15.3% (95% CI: 13.2-18.4%)"

**Correct:**
"Mean overhead was 15.3% (95% CI: 13.2-18.4%, **n=5**)"

**Why it matters:**
Reviewers need to assess the reliability of your estimates. A CI with n=5 has very different implications than one with n=50.

### ❌ Mistake 5: Over-interpreting with Low Power

With n=3-5, statistical power is low. This means:

- **Non-significant ≠ No difference** (may lack power to detect real differences)
- **Significant = Strong evidence** (if you find it with small n, it's robust)

**Be cautious with statements like:**
- ❌ "There is no difference between methods" (could be insufficient power)
- ✅ "We found no statistically significant difference, though small sample sizes limit our power to detect differences"

---

## Examples and Best Practices

### Example 1: Comparing Experimental vs Theoretical Values

```python
from ciStatistics import compare_with_theoretical

experimental_values = [14.2, 15.8, 16.1, 14.9, 15.3]  # n=5
theoretical_value = 12.5

result = compare_with_theoretical(experimental_values, theoretical_value)

print(result['interpretation'])
# Output: "Experimental mean (15.26) differs significantly from
#          theoretical value (12.5), p=0.003"
```

**In your paper:**
> The experimental overhead (15.3%, 95% CI [14.4, 16.2], n=5) was significantly higher than the theoretical prediction (12.5%), t(4) = 5.23, p = 0.003, indicating additional overhead from retransmissions.

### Example 2: Small Sample with Individual Points

```python
import matplotlib.pyplot as plt
import numpy as np

configs = ['Config A', 'Config B', 'Config C']
means = [15.3, 22.1, 18.7]
ci_lowers = [13.2, 19.5, 16.8]
ci_uppers = [17.4, 24.7, 20.6]
individual_points = [
    [14.2, 15.8, 16.1, 14.9, 15.3],
    [20.5, 22.8, 21.9, 23.1, 22.2],
    [17.8, 19.2, 18.5, 19.9, 18.1]
]

x = np.arange(len(configs))
errors = [np.array(ci_uppers) - np.array(means),
          np.array(means) - np.array(ci_lowers)]

# Bar plot with error bars
plt.bar(x, means, yerr=errors, capsize=5, alpha=0.7, label='Mean ± 95% CI')

# Overlay individual points
for i, points in enumerate(individual_points):
    plt.scatter([i] * len(points), points, color='red',
                s=50, zorder=3, alpha=0.6, label='Individual runs' if i == 0 else '')

plt.xlabel('Configuration')
plt.ylabel('Overhead (%)')
plt.title('Experimental Overhead (n=5 per config)')
plt.xticks(x, configs)
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()
plt.savefig('overhead_with_points.png', dpi=300)
```

### Example 3: Complete Publication Export

```python
from Testing.ui.exportResults import export_all_formats

# Your results from experiments
results = [
    {
        'label': 'Reliable_MultiSender_100pkt_50B',
        'n': 5,
        'metrics': {
            'Theoretical Overhead': 24.5,  # Single value
            'Experimental Overhead': {
                'mean': 28.3,
                'std': 2.1,
                'ci_lower': 25.7,
                'ci_upper': 30.9,
                'values': [26.2, 28.1, 29.5, 27.8, 29.9]
            },
            'Packets Lost': {
                'mean': 3.2,
                'std': 0.8,
                'ci_lower': 2.2,
                'ci_upper': 4.2,
                'values': [2.8, 3.5, 3.1, 3.9, 2.7]
            }
        }
    }
]

# Export everything
output_files = export_all_formats(
    results=results,
    output_dir='./paper_results',
    base_filename='lora_performance_jan2025',
    confidence_level=0.95,
    table_caption='LoRa Mesh Network Performance Under Reliable Multi-Sender Configuration'
)

# Generates:
# - lora_performance_jan2025.tex (LaTeX table)
# - lora_performance_jan2025.csv (data file)
# - lora_performance_jan2025.md (markdown table)
# - lora_performance_jan2025_summary.txt (statistics)
# - lora_performance_jan2025_methods.txt (Methods section text)
```

---

## FAQ

### Q: My CIs are very wide. Is something wrong?

**A:** No! With n=3-5, wide CIs are **expected and appropriate**. They honestly reflect the uncertainty in your estimate. This is scientifically rigorous.

**What to do:**
- Report them honestly
- Show individual data points for transparency
- Acknowledge in Discussion: "The wide CIs reflect limited sample sizes due to computational constraints"
- Collect more data if possible (but n=5 is often acceptable)

### Q: Can I use 90% CI instead of 95% to make them narrower?

**A:** Technically yes, but 95% is the standard in most fields. Using 90% may raise reviewer concerns unless you have a good justification.

**When 90% CI is acceptable:**
- Preliminary/exploratory studies
- When explicitly stated and justified
- Field-specific conventions (some engineering fields use 90%)

### Q: Should I exclude outliers?

**A:** Be very cautious. With n=3-5, one "outlier" might be 20-33% of your data!

**Decision tree:**
1. Is there a clear experimental error (equipment failure, etc.)? → Can exclude with justification
2. Is the value simply higher/lower than expected? → **Keep it**
3. When in doubt → Report both with and without the point

**Always:**
- Document outlier handling in Methods
- Report how many points were excluded
- Consider showing data with and without outliers

### Q: Normality test says data is non-normal. What do I do?

**A:** With n=3-5, normality tests have very low power. Don't overreact!

**Options:**
1. **If p > 0.05 (test says normal):** Use parametric CI confidently
2. **If p < 0.05 (test says non-normal):**
   - Visual inspection (histogram, Q-Q plot)
   - Consider bootstrap CI as sensitivity analysis
   - If parametric and bootstrap CIs are similar → report parametric with note
   - If very different → report both or discuss as limitation

### Q: Can I pool data from slightly different configurations?

**A:** Generally **no**. This violates independence assumptions and can give falsely narrow CIs.

**Only pool if:**
- Configurations are truly identical (different seeds only)
- You have theoretical justification
- You report that data was pooled

---

## Additional Resources

### Recommended Reading

1. **Cumming, G. (2014).** "The New Statistics: Why and How." *Psychological Science*, 25(1), 7-29.
   - Excellent introduction to CIs and why they're better than p-values alone

2. **Altman, D.G., et al. (2000).** "Statistics with Confidence" (2nd ed.). BMJ Books.
   - Comprehensive guide to confidence intervals in practice

3. **Smithson, M. (2003).** "Confidence Intervals." Sage Publications.
   - Detailed but accessible treatment

### Online Tools

- **GraphPad CI Calculator:** https://www.graphpad.com/quickcalcs/
- **Sample Size Calculator:** https://www.stat.ubc.ca/~rollin/stats/ssize/
- **Statistical Power Calculator:** https://www.stat.uiowa.edu/~rlenth/Power/

### Getting Help

If you have questions about your specific analysis:

1. Check this guide first
2. Look at the generated `_methods.txt` and `_summary.txt` files
3. Consult with a statistician if making non-standard choices
4. Be transparent in your paper about any deviations from standard practice

---

## Summary Checklist for Your Paper

Before submission, verify:

- [ ] CIs calculated using Student's t-distribution (not z-scores)
- [ ] Sample sizes (n) reported in all figures and tables
- [ ] Individual data points shown for n < 5
- [ ] Methods section describes CI calculation method
- [ ] Appropriate references cited (Student 1908, etc.)
- [ ] Wide CIs acknowledged as appropriate for sample size
- [ ] Normality testing performed and reported (if applicable)
- [ ] No claims of "precision" with very wide CIs
- [ ] Confidence level stated (95% assumed if not stated)
- [ ] Figure captions explain what error bars represent
- [ ] Data availability statement (raw data in supplement/repository)

---

## Version History

- **v1.0 (2025-01-24):** Initial guide created for LoRaChat testing framework
  - Focused on small sample sizes (n=3-5)
  - Integration with ciStatistics.py and exportResults.py modules
  - Publication-ready export examples

---

**Questions or suggestions?** Contact the LoRaChat development team or open an issue on GitHub.

---

**This guide is provided as-is for educational purposes. Always consult with a statistician for critical analyses or when making non-standard statistical choices.**
