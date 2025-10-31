-- Monthly revenue with trend
SELECT 
    department,
    TS_FORECAST(month, revenue, 'Holt', 12, MAP{'alpha': 0.3, 'beta': 0.1}) AS forecast
FROM financial_data
GROUP BY department;
