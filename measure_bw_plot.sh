#!/bin/sh 


plotin=$(mktemp XXX$$)

for fin in "$@"; do 
    cat ${fin} | gzip -dc | sed -n '/MEASUREBW/,$p'| sed 's/k/000/g' > $plotin
    SHORT=$(cat $plotin | head -n1 | awk '{print $2}')
    LONG=$(cat $plotin | head -n1 | awk '{print $3}')
    cat $plotin | grep -Ev '(MEASUREBW|DNF)' > $plotin.0 
    cat $plotin.0 | awk 'n==0{t0=$1;n++;} {$1-=t0; print $0}' > measure_bw.plot.in
    WLAN0=$(cat  measure_bw.plot.in  | awk '{print $2}' | sort | uniq | grep -v multi| head -n1) 
    WLAN1=$(cat  measure_bw.plot.in  | awk '{print $2}' | sort | uniq | grep -v multi| tail -n1) 
    t0=$(cat ${plotin}.0| head -n1 | awk '{print $1}') 
    t0=$(date -d @$t0)
    echo "t0=$t0 $WLAN0 $WLAN1"
    cat measure_bw.plot | sed -e "s|TITLE|$t0|g"  \
 -e "s|WLAN0|${WLAN0}|g"  -e "s|WLAN1|${WLAN1}|g" \
 -e "s|SHORT|${SHORT}|g"  -e "s|LONG|${LONG}|g" \
| gnuplot
    mv short.png ${fin}_short.png 
    mv long.png ${fin}_long.png 
done 

rm $plotin $plotin.0
rm measure_bw.plot.in
 
