#set terminal latex
#set output "graph.tex"
#set format xy "$%g$"
set terminal png
set output "graph.png"
set title "Rhizome Delivery Delay"
set logscale xy
set ylabel "Delivery Latency (seconds)"
set xlabel "Number of Rhizome Bundles on Each Node"
set key at 50,5000
set datafile separator ","
set yrange [5:100000]
set xrange [1:2200]
plot "nopriority.csv" u ($1):($2)/1000 with linespoints title "without prioritisation", "withpriority.csv"  u ($1):($2)/1000 with linespoints title "with prioritisation"
set terminal latex
set output "graph.tex"
set format xy "$%g$"
plot "nopriority.csv" u ($1):($2)/1000 with linespoints title "without prioritisation", "withpriority.csv"  u ($1):($2)/1000 with linespoints title "with prioritisation"
