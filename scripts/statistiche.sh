#!/bin/bash

echo ""
BASEDIR="$(cd "$(dirname "$(dirname "${BASH_SOURCE[0]}")")" && pwd)"
if [ $# -eq 0 ]; then
	echo "< Log file not specified. Using default log file"
	LOG_FILE=$BASEDIR/logs/log.txt
	else
	    LOG_FILE=$1
fi

# Se il file è vuoto esco
[ -s "$LOG_FILE" ] && echo "< Parsing log file..." || exit 1
echo ""

#READ/READ_N
declare -i sum=0;
read_op=$(grep -c "/OP/=READ" "$LOG_FILE")
read_ok=$(grep "/OP/=READ" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
sum=$(grep "/OP/=READ" "$LOG_FILE" | grep "/OUTCOME/=OK" | cut -d ' ' -f6 | cut -d '=' -f2 |  awk '{ SUM += $1} END { print SUM }')
echo "Read operations requested:" "$read_op"
echo "Read operations completed with success:" "$read_ok"
echo "Total bytes read:" "$sum"
if [ "${read_ok}" -gt 0 ]; then
  read_avg=$(echo "scale=2; ${sum} / ${read_ok}" | bc -l)
  echo "Average bytes read:" "$read_avg"
fi
echo ""

#WRITE/WRITE_APPEND
declare -i sum=0;
write_op=$(grep -c "/OP/=WRITE" "$LOG_FILE")
write_ok=$(grep "/OP/=WRITE" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
sum=$(grep "/OP/=WRITE" "$LOG_FILE" | grep "/OUTCOME/=OK" | cut -d ' ' -f5 | cut -d '=' -f2 |  awk '{ SUM += $1} END { print SUM }')
echo "Write operations requested:" "$write_op"
echo "Write operations completed with success:" "$write_ok"
echo "Total bytes written:" "$sum"
if [ "${write_ok}" -gt 0 ]; then
write_avg=$(echo "scale=2; ${sum} / ${write_ok}" | bc -l)
echo "Average byte written:" "$write_avg"
fi
echo ""

#LOCK
lock_op=$(grep -c "/OP/=LOCK" "$LOG_FILE" )
lock_ok=$(grep "/OP/=LOCK" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
openlock_op=$(grep -c "/OP/=OPEN_LOCK" "$LOG_FILE")
openlock_ok=$(grep "/OP/=OPEN_LOCK" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
opencreatelock_op=$(grep -c "/OP/=OPEN_CREATE_LOCK" "$LOG_FILE")
opencreatelock_ok=$(grep "/OP/=OPEN_CREATE_LOCK" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
echo "Lock operations requested:" "$lock_op"
echo "Lock operations completed with success:" "$lock_ok"
echo "Lock requested when opening/creating a file:" $((openlock_op+opencreatelock_op))
echo "Lock completed when opening/creating a file:" $((openlock_ok+opencreatelock_ok))
echo "Total locks completed with success:" $((openlock_ok+opencreatelock_ok+lock_ok))
echo ""

#UNLOCK
unlock_op=$(grep -c "/OP/=UNLOCK" "$LOG_FILE")
unlock_ok=$(grep "/OP/=UNLOCK" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
echo "Unlock operations requested:" "$unlock_op"
echo "Unlock operations completed with success:" "$unlock_ok"
echo ""

#OPEN
open_op=$(grep -c "/OP/=OPEN" "$LOG_FILE")
open_ok=$(grep "/OP/=OPEN" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
echo "Open operations requested:" "$open_op"
echo "Open operations completed with success:" "$open_ok"
echo ""

#CLOSE
close_op=$(grep -c "/OP/=CLOSE" "$LOG_FILE")
close_ok=$(grep "/OP/=CLOSE" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
echo "Close operations requested:" "$close_op"
echo "Close operations completed with success:" "$close_ok"
echo ""

#MAX CAPACITY
max_capacity_bytes=$(grep "/OP/=MAXCAPACITY" "$LOG_FILE" | cut -d ' ' -f5 | cut -d '=' -f2)
max_capacity_mb=$(echo "scale=4; ${max_capacity_bytes} / 1000000" | bc -l)
echo "Maximum storage capacity occupied:" "$max_capacity_mb" "MB"

#MAX FILES
max_files=$(grep "/OP/=MAXFILES" "$LOG_FILE" | cut -d ' ' -f5 | cut -d '=' -f2)
echo "Maximum files stored:" "$max_files"

#REPLACEMENT ALGORITHM
replacement_times=$(grep "/OP/=VICTIM" "$LOG_FILE" | grep -c "/OUTCOME/=OK")
echo "Replacement algorithm executed times:" "$replacement_times"
echo ""

total_requests=$(grep -v "/OP/=CONNECT" "$LOG_FILE" | grep -v "/OP/=DISCONNECT" | grep -v "/OP/=MAXFILES" | grep -vc "/OP/=MAXCAPACITY")
echo "Total requests received:" "$total_requests"

#THREADS
grep -v "/OP/=CONNECT" "$LOG_FILE" | grep -v "/OP/=DISCONNECT" | grep -v "/OP/=MAXFILES" | grep -v "/OP/=MAXCAPACITY" | \
cut -d ' ' -f1 | cut -d '=' -f2 | sort | uniq -c | awk '{print "Requests handled by thread worker ["$2"]: "$1}'

#CONNESSIONI
#cerco il minimo ed il massimo fd durante l'esecuzione
#la loro differenza+1 corrisponde al numero di client connessi contemporaneamente
min_client=$( grep -v "/OP/=MAXFILES" "$LOG_FILE" | grep -v "/OP/=MAXCAPACITY" | cut -d ' ' -f3 | cut -d '=' -f2 | sort -g | head -1)
max_client=$( grep -v "/OP/=MAXFILES" "$LOG_FILE" | grep -v "/OP/=MAXCAPACITY" | cut -d ' ' -f3 | cut -d '=' -f2 | sort -g | tail -1)
max_connection=$((max_client-min_client+1))
echo "Maximum clients connected at the same time: "$max_connection
