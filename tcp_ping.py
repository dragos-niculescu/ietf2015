import time,sys 
from datetime import datetime
from socket import *

s = socket(AF_INET, SOCK_STREAM)
if len(sys.argv) == 1:
    destination = "macin.cs.pub.ro"
else:
    destination = sys.argv[1]

print "TCP ping to", destination, "443"

s.connect((destination, 443))

while 1:
    before = datetime.now()
    s.send((time.ctime() + '\n').encode())
    data = s.recv(4096)
    after = datetime.now()
    data = data.decode()
#    print data, (after - before).total_seconds()
    print (after-before).seconds*1000.0 + (after-before).microseconds/1000.0, "ms"

    time.sleep(1.0)
