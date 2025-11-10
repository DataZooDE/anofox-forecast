# Guide Index - Complete Documentation

## Welcome

This directory contains comprehensive guides for using anofox-forecast, organized by topic and complexity level.

**Total Guides**: 16 comprehensive documents  
**Total Pages**: 6,500+ lines of documentation  
**Skill Levels**: Beginner → Advanced  
**Perspectives**: Technical, Statistical, Business, Multi-Language  
**Languages**: SQL, Python, R, Julia, C++, Rust  

---

## 🚀 Getting Started (Beginners)

### 1. [Quick Start Guide](01_quickstart.md) ⭐

**5 minutes to your first forecast**

- Load extension
- Create sample data
- Generate first forecast
- Multiple series example

**Skill Level**: Beginner  
**Time**: 5 minutes  
**Prerequisites**: None  

### 2. [Basic Forecasting](30_basic_forecasting.md)

**Complete workflow from data to deployment**

- Data preparation steps
- Seasonality detection
- Model selection basics
- Forecast evaluation
- Real-world example

**Skill Level**: Beginner  
**Time**: 30 minutes  
**Prerequisites**: Quick Start  

---

## 📖 Technical Guides (Developers & Data Scientists)

### 10. [API Reference](90_api_reference.md) ⭐

**Complete function documentation**

- All 31 models listed
- All 12 metrics documented
- EDA & data prep macros
- Parameter reference
- Return types
- Error handling

**Skill Level**: All levels  
**Use**: Reference document  

### 11. [Model Selection Guide](40_model_selection.md)

**Choosing the right forecasting model**

- Decision tree
- Model comparison matrix
- Strengths & weaknesses
- Use case examples
- Parameter tuning guide

**Skill Level**: Intermediate  
**Time**: 45 minutes  
**Prerequisites**: Basic Forecasting  

### 13. [Performance Optimization](60_performance_optimization.md)

**Scaling to millions of series**

- Performance benchmarks
- Parallelization strategies
- Memory optimization
- Query optimization
- Hardware recommendations
- Profiling techniques

**Skill Level**: Advanced  
**Time**: 60 minutes  
**Prerequisites**: API Reference  

### 40. [EDA & Data Preparation](11_exploratory_analysis.md) ⭐

**Data quality and preparation workflow**

- Why data prep matters
- EDA workflow (5 steps)
- Data preparation techniques
- Common issues & solutions
- Quality metrics
- Automation strategies

**Skill Level**: Intermediate  
**Time**: 60 minutes  
**Prerequisites**: Basic Forecasting  

---

## 📊 Statistical Guides (Statisticians & Analysts)

### 20. [Understanding Forecasts](31_understanding_forecasts.md) ⭐

**Statistical concepts explained**

- Time series components
- Point forecasts vs intervals
- Confidence intervals interpretation
- Residual analysis
- Model diagnostics
- Autocorrelation
- Stationarity

**Skill Level**: Intermediate  
**Time**: 60 minutes  
**Prerequisites**: Basic statistics  

### 21. Accuracy Metrics (Coming Soon)

**Deep dive into evaluation metrics**

- MAE, RMSE, MAPE, SMAPE
- R², BIAS, MASE
- Coverage analysis
- When to use each metric
- Benchmark values

### 22. Confidence Intervals (Coming Soon)

**Understanding uncertainty quantification**

- Prediction vs confidence intervals
- Coverage calibration
- Interval width analysis
- Risk assessment

### 23. Seasonality Analysis (Coming Soon)

**Detecting and modeling seasonal patterns**

- Periodogram analysis
- ACF-based detection
- Multiple seasonality
- Seasonal decomposition

---

## 💼 Business Guides (Business Users & Managers)

### 30. [Demand Forecasting](70_demand_forecasting.md) ⭐

**Retail & inventory optimization**

- Business ROI examples
- Inventory optimization
- Safety stock calculation
- ABC classification
- Reorder point calculation
- Dashboards & KPIs

**Skill Level**: Business user  
**Time**: 45 minutes  
**Prerequisites**: None  
**Business Value**: High  

### 31. [Sales Prediction](71_sales_prediction.md) ⭐

**Revenue forecasting for planning**

- Quarterly projections
- Growth analysis
- Risk management (VaR)
- Scenario planning
- Target tracking
- Executive dashboards

**Skill Level**: Business user  
**Time**: 45 minutes  
**Prerequisites**: None  
**Business Value**: High  

### 32. [Capacity Planning](72_capacity_planning.md) ⭐

**Resource optimization**

- Workforce planning
- Manufacturing capacity
- IT infrastructure sizing
- Warehouse space planning
- Energy generation
- Cost optimization

**Skill Level**: Business user  
**Time**: 45 minutes  
**Prerequisites**: None  
**Business Value**: Very High  

### 33. Anomaly Detection (Coming Soon)

**Identifying outliers and anomalies**

- Real-time anomaly detection
- Alert systems
- Root cause analysis

---

## 🎯 Advanced Topics

### 41. [Changepoint Detection](docs/../examples/changepoint_detection.sql)

**Regime identification and analysis**

- Bayesian changepoint detection
- Probability scoring
- Regime-based features
- Adaptive forecasting
- Multi-series examples

**Skill Level**: Advanced  
**Time**: 90 minutes  
**Prerequisites**: Basic Forecasting, Understanding Forecasts  

### 42. Hierarchical Forecasting (Coming Soon)

**Multi-level forecasting and reconciliation**

- Top-down vs bottom-up
- Reconciliation methods
- Aggregation strategies

### 43. Model Ensembles (Coming Soon)

**Combining multiple models**

- Weighted averaging
- Stacking approaches
- Ensemble selection

---

## 🌍 Multi-Language Guides (Language Bindings)

### 49. [Multi-Language Overview](80_multi_language_overview.md) ⭐

**Write SQL once, use everywhere**

- Language comparison
- Integration patterns
- Polyglot workflows
- Performance comparison
- Choosing the right language

**Skill Level**: All levels  
**Time**: 20 minutes  
**Key Insight**: Same SQL works in all languages!  

### 50. [Python Usage](81_python_integration.md)

**Using from Python (pandas, polars)**

- Installation & setup
- DataFrame integration
- Visualization (matplotlib, plotly)
- API patterns (FastAPI)
- Batch processing
- Integration examples

**Skill Level**: Intermediate  
**Time**: 30 minutes  
**Prerequisites**: Python basics  

### 51. [R Usage](82_r_integration.md)

**Using from R (tidyverse, ggplot2)**

- Installation & setup
- data.frame integration
- Visualization (ggplot2)
- RMarkdown reports
- Shiny dashboards
- Plumber APIs

**Skill Level**: Intermediate  
**Time**: 30 minutes  
**Prerequisites**: R basics  

### 52. [Julia Usage](83_julia_integration.md)

**Using from Julia (DataFrames.jl)**

- Installation & setup
- DataFrame integration
- Type-safe queries
- Visualization (Plots.jl)
- HTTP APIs (Oxygen.jl)
- Scientific computing

**Skill Level**: Intermediate  
**Time**: 30 minutes  
**Prerequisites**: Julia basics  

### 53. [C++ Usage](84_cpp_integration.md)

**Using from C++ (embedded)**

- CMake integration
- Type-safe results
- Error handling
- High-performance services
- Prepared statements
- Production patterns

**Skill Level**: Advanced  
**Time**: 40 minutes  
**Prerequisites**: C++ knowledge  

### 54. [Rust Usage](85_rust_integration.md)

**Using from Rust (safe & fast)**

- Cargo setup
- Type-safe structures
- Error handling (Result)
- Async with Tokio
- Web services (Actix)
- CLI applications

**Skill Level**: Advanced  
**Time**: 40 minutes  
**Prerequisites**: Rust knowledge  

---

## 📋 Quick Reference by Use Case

### "I want to..."

**...forecast sales for my retail store**
→ [Demand Forecasting](70_demand_forecasting.md)

**...predict revenue for financial planning**
→ [Sales Prediction](71_sales_prediction.md)

**...plan staffing levels**
→ [Capacity Planning](72_capacity_planning.md)

**...choose the right model**
→ [Model Selection](40_model_selection.md)

**...understand what the forecast means**
→ [Understanding Forecasts](31_understanding_forecasts.md)

**...clean my messy data**
→ [EDA & Data Prep](11_exploratory_analysis.md)

**...forecast 10,000 products quickly**
→ [Performance Optimization](60_performance_optimization.md)

**...detect when demand patterns change**
→ [Changepoint Detection](41_changepoint_detection.md)

**...use from Python/R/Julia/C++/Rust**
→ [Multi-Language Overview](80_multi_language_overview.md) ⭐

**...build a Python API**
→ [Python Usage](81_python_integration.md)

**...create an R Shiny dashboard**
→ [R Usage](82_r_integration.md)

**...embed in a C++ application**
→ [C++ Usage](84_cpp_integration.md)

**...build a safe Rust service**
→ [Rust Usage](85_rust_integration.md)

**...look up a specific function**
→ [API Reference](90_api_reference.md)

**...get started in 5 minutes**
→ [Quick Start](01_quickstart.md)

---

## 📚 Learning Paths

### Path 1: Business User

1. [Quick Start](01_quickstart.md) - 5 min
2. [Demand Forecasting](70_demand_forecasting.md) OR [Sales Prediction](71_sales_prediction.md) - 45 min
3. [Capacity Planning](72_capacity_planning.md) - 45 min
4. [Understanding Forecasts](31_understanding_forecasts.md) - 60 min

**Total time**: ~2.5 hours  
**Outcome**: Can forecast and interpret results for business decisions  

### Path 2: Data Scientist

1. [Quick Start](01_quickstart.md) - 5 min
2. [Basic Forecasting](30_basic_forecasting.md) - 30 min
3. [Understanding Forecasts](31_understanding_forecasts.md) - 60 min
4. [Model Selection](40_model_selection.md) - 45 min
5. [EDA & Data Prep](11_exploratory_analysis.md) - 60 min
6. [Performance Optimization](60_performance_optimization.md) - 60 min

**Total time**: ~4.5 hours  
**Outcome**: Expert-level forecasting with optimization skills  

### Path 3: Engineer/Developer

1. [Quick Start](01_quickstart.md) - 5 min
2. [API Reference](90_api_reference.md) - 30 min
3. [Performance Optimization](60_performance_optimization.md) - 60 min
4. [EDA & Data Prep](11_exploratory_analysis.md) - 60 min

**Total time**: ~2.5 hours  
**Outcome**: Can integrate forecasting into production systems  

---

## 📊 Documentation Statistics

### Coverage

| Category | Guides | Lines | Topics |
|----------|--------|-------|--------|
| **Getting Started** | 2 | ~600 | Quickstart, basics |
| **Technical** | 4 | ~1,400 | API, models, performance, EDA |
| **Statistical** | 1 | ~600 | Concepts, metrics, intervals |
| **Business** | 3 | ~1,200 | Demand, sales, capacity |
| **Advanced** | 1 | ~700 | Changepoints, regimes |
| **Multi-Language** | 6 | ~3,000 | Python, R, Julia, C++, Rust, Overview |
| **Total** | **17** | **~7,500** | **All aspects** |

### Example Code

- **Total SQL examples**: 150+
- **Complete workflows**: 30+
- **Use cases covered**: 20+

---

## 🎓 Additional Resources

### Examples Directory

```
examples/
├── insample_forecast_demo.sql      - In-sample forecasts & confidence levels
├── eda_data_prep_demo.sql          - EDA & data preparation
├── changepoint_detection.sql       - Changepoint analysis (12 steps!)
├── seasonality_detection.sql       - Seasonality examples
└── rolling_forecast.sql            - Rolling window forecasting
```

### Documentation Directory

```
docs/
├── 50_evaluation_metrics.md                      - All 12 metrics documented
├── 42_insample_validation.md            - Fitted values guide
├── 20_data_preparation.md                - EDA & prep reference
├── 12_detecting_seasonality.md              - Seasonality functions
├── 13_detecting_changepoints.md              - Changepoint functions
└── 41_model_parameters.md                   - Parameter details
```

---

## 🆘 Getting Help

### By Experience Level

**Beginners**: Start with [Quick Start](01_quickstart.md)

**Intermediate**: Go to [guide that matches your use case](#quick-reference-by-use-case)

**Advanced**: Check [API Reference](90_api_reference.md) and [Performance](60_performance_optimization.md)

### By Question Type

**"How do I...?"** → Search this index or [API Reference](90_api_reference.md)

**"Which model...?"** → [Model Selection](40_model_selection.md)

**"What does this mean...?"** → [Understanding Forecasts](31_understanding_forecasts.md)

**"My forecast is slow..."** → [Performance](60_performance_optimization.md)

**"My forecast is inaccurate..."** → [EDA & Data Prep](11_exploratory_analysis.md)

**"How can I use this for...?"** → [Business Guides](#business-guides-business-users--managers)

---

## 📝 Contributing

Found an error? Have a suggestion?

- Open an issue on GitHub
- Submit a pull request
- Contact: <docs@anofox.com>

---

## ✅ Checklist: Am I Ready to Forecast?

Before you start forecasting in production:

- [ ] Read [Quick Start](01_quickstart.md)
- [ ] Understand your data (run TS_STATS, TS_QUALITY_REPORT)
- [ ] Choose appropriate model ([Model Selection](40_model_selection.md))
- [ ] Validate on holdout data
- [ ] Check forecast accuracy (MAPE < 20%)
- [ ] Verify interval calibration (coverage ≈ confidence level)
- [ ] Set up monitoring dashboard
- [ ] Document business logic and assumptions
- [ ] Plan for reforecasting frequency
- [ ] Have rollback plan for poor forecasts

---

**Happy Forecasting!** 📈

Start here: [Quick Start Guide](01_quickstart.md) → Get your first forecast in 5 minutes!

**Questions?** Check the appropriate guide above or contact <support@anofox.com>
