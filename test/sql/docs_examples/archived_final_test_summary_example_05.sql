   -- ❌ Doesn't work:
   SELECT t.group_col FROM ... t
   
   -- ✅ Works:
   WITH aliased AS (SELECT group_col AS __g FROM ...)
   SELECT __g AS group_col FROM aliased
