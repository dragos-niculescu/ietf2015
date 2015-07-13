#!/usr/bin/gnuplot 

#The following strings *MUST* be replaced before sending to gnuplot:
#  TITLE - usually start time
#  SHORT - length in bytes  
#  LONG - length in bytes
#  WLAN0 - card name 
#  WLAN1 - card name 

set xlabel "Time[min]"
set ylabel "Throughput [Mbps]"
set title "TITLE SHORT bytes" 
set key top left
set xtics 600

set term png size 1024,600 
set out "short.png"


plot \
"< cat measure_bw.plot.in | grep 'WLAN0.*SHORT'" using ($1/60):($4*8/10e6) w l lw 3 t "WLAN0", \
"< cat measure_bw.plot.in | grep 'WLAN1.*SHORT'" using ($1/60):($4*8/10e6) w l lw 3 t "WLAN1", \
"< cat measure_bw.plot.in | grep 'multi.*SHORT'" using ($1/60):($4*8/10e6) w l lw 3 t "multi"

set title "TITLE LONG bytes" 
set out "long.png"
plot \
"< cat measure_bw.plot.in | grep 'WLAN0.*LONG'" using ($1/60):($4*8/10e6) w l lw 3 t "WLAN0",  \
"< cat measure_bw.plot.in | grep 'WLAN1.*LONG'" using ($1/60):($4*8/10e6) w l lw 3 t "WLAN1", \
"< cat measure_bw.plot.in | grep 'multi.*LONG'" using ($1/60):($4*8/10e6) w l lw 3 t "multi" 
 
