-- Limit threads (if running alongside other workloads)
SET threads TO 8;

-- Reset to default (all cores)
RESET threads;
