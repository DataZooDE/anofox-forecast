-- Time each step
.timer on
CREATE TABLE step1 AS SELECT * FROM TS_FILL_GAPS(...);
-- Note time
CREATE TABLE step2 AS SELECT * FROM TS_DROP_CONSTANT('step1', ...);
-- Note time
