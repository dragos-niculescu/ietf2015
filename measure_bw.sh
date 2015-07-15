#!/bin/sh 

# how many bytes to get from the URL
# SHORT SHOULDNOT be a regex prefix of LONG 
#
LONG=2000000
SHORT=5000
LENGTH=$LONG

#server with MPTCP, with file at least $LONG bytes
URL="http://141.85.37.151/1G"

PIDFILE=/tmp/measure_bw.pid
OUTFILE=/tmp/measure_bw.out
STATSFILE=/tmp/measure_bw.stats


DATE=$(date "+%s")

start()
{
    echo "$DATE $IFACE $LENGTH " > $OUTFILE
    if [ $IFACE = "multi" ]; then 
	ip link set dev eth0 multipath off
	sysctl net.mptcp.mptcp_enabled=1
	CMDIFACE=""
    else
	sysctl net.mptcp.mptcp_enabled=0
	CMDIFACE="--interface $IFACE"
    fi
    curl $CMDIFACE --range 1-$LENGTH --limit-rate 10G $URL > /dev/null 2>> $OUTFILE & 
    echo $! > $PIDFILE 
}

stop()
{
    #get last line of curl output, field 7(average throughput) 
    BPS=$(cat $OUTFILE| tr '' '\n'| tail -n1 | awk '{print $7}')
    if [ "x${BPS}x" = "xx" ]; then 
	BPS="DNF"
    fi
    echo $(head -n 1 $OUTFILE) $BPS >> $STATSFILE 
    kill -9 $(cat $PIDFILE)
}

crontab()
{
# CRONTAB example: short=1min long=5min  
echo '
#example to put in "crontab -e": set WLAN0 and WLAN1 to actual interfaces

#short=1min long=2min
WLAN0=wlan0
WLAN1=v1_wlan0
0,10,20,30,40,50  * * * * measure_bw.sh start_short $WLAN0   
1,11,21,31,41,51  * * * * measure_bw.sh stop; measure_bw.sh start_short $WLAN1   
2,12,22,32,42,52  * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN0   
4,14,24,34,44,54  * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN1   
6,16,26,36,46,56 * * * * measure_bw.sh stop; measure_bw.sh start_short multi
7,17,27,37,47,57 * * * * measure_bw.sh stop; measure_bw.sh start_long multi   
9,19,29,39,49,59 * * * * measure_bw.sh stop; measure_bw.sh upload 
'
#short=1min long=5min
# WLAN0=wlan0
# WLAN1=v1_wlan0
# 0,20,40  * * * * measure_bw.sh start_short $WLAN0   
# 1,21,41  * * * * measure_bw.sh stop; measure_bw.sh start_short $WLAN1   
# 2,22,42  * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN0   
# 7,27,47  * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN1   
# 12,32,52 * * * * measure_bw.sh stop; measure_bw.sh start_short multi
# 13,33,53 * * * * measure_bw.sh stop; measure_bw.sh start_long multi   
# 18,38,58 * * * * measure_bw.sh stop; measure_bw.sh upload 

}

upload()
{
    ID=$(sudo dmidecode -t 4 | grep ID | sed 's/.*ID://;s/ //g') 
    ETH=$(/sbin/ifconfig -a | grep -oP 'Ether.*HWaddr \K.*' | head -n1 | tr -d '[\n :]' )
    { echo "SUBMITFROM $ID $ETH $DATE"; hostname; date -d "@$DATE"; ifconfig -a; iwconfig; ip ro; ip ru sh; sysctl -a; dmesg; lsmod; } > /tmp/$ID-$ETH-$DATE
    echo "MEASUREBW $SHORT $LONG" >> /tmp/$ID-$ETH-$DATE
    cat $STATSFILE >> /tmp/$ID-$ETH-$DATE
    gzip /tmp/$ID-$ETH-$DATE
    curl -F "uploadedfile=@/tmp/$ID-$ETH-$DATE.gz" http://141.85.37.151/static/ietf/upload.php
    
}


usage()
{
    echo "Usage: $0 {start_long|start_short|stop|pid|upload|crontab} {interface=wlan0|wlan1|v1_wlan0|...}"
    exit 1
}

#########################################


case "$1" in
start_short)
	LENGTH=$SHORT
	if [ $# -eq 2 ]; then 
	    IFACE=$2
            start
	else
	    usage
	fi
        ;;
start_long)
	LENGTH=$LONG
	if [ $# -eq 2 ]; then 
	    IFACE=$2
            start
	else
	    usage
	fi
        ;;
stop)
        stop
        ;;
pid)
        cat $PIDFILE
        ;;
crontab)
	crontab
	;;
upload)
	upload
	;;
*)
	usage
esac





