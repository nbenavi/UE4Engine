use crashreport
SELECT *
from buggs
where [TimeOfFirstCrash] > CAST(GETDATE()-1 AS DATE)
order by TimeOfFirstCrash
