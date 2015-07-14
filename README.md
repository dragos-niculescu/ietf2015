# WiFi Mobility #

This is a mac80211 patch which can be used to eliminate the handoff process in 802.11 and maintain active TCP streams in the case of mobile clients, effectively granting mobility over wireless networks by constantly switching channels.

### Summary ###

* One device, multi-channel
* Can be used with network-manager
* Vendor-independent - all modifications are within mac80211 and cfg80211
* Dynamic - channel switching is triggered when associating with multiple access points

### Requirements ###

This has been tested on Ubuntu 14.04.02 LTS using an Atheros 9k.

* Download and install [Ubuntu 14.04 LTS](http://www.ubuntu.com/download/desktop)
* install some needed packages 
```
apt-get install curl build-essential
```
* Set up [MPTCP](http://multipath-tcp.org/pmwiki.php/Users/HowToInstallMPTCP?) and reboot
```
```
  * patch works for other Ubuntu versions (precise), but make sure you use trusty repos, so that kernel 3.14.0-89 is used
  * ```wget -q -O - http://multipath-tcp.org/mptcp.gpg.key | sudo apt-key add - ```
  * ```echo 'deb http://multipath-tcp.org/repos/apt/debian trusty main' >> /etc/apt/sources.list.d/mptcp.list```
  * ```apt-get update && apt-get install linux-mptcp ```
  * postpone routing section till after you have the virtual wireless interface, see below routing section
    * ```[mptcp_up](https://github.com/multipath-tcp/mptcp-scripts/raw/master/scripts/rt_table/mptcp_up) - Place it inside /etc/network/if-up.d/ and make it executable.```
    * ```[mptcp_down](https://github.com/multipath-tcp/mptcp-scripts/raw/master/scripts/rt_table/mptcp_down) - Place it inside /etc/network/if-post-down.d/ and make it executable.```

  * reboot
* clone driver updates and scripts 

```
sudo bash 
git clone https://github.com/dragos-niculescu/ietf2015.git 
cd ietf2015
```  


### Setting up ###

#### Building ####

* Turning off wireless from the GUI.
* Unload wireless drivers and build new 802.11 module

```
service network-manager stop 
./wm unload 
./wm build
```
#### Adding virtual interfaces ####

```
./wm vifs add <wireless_interface_name> <index>
```

Vif names will typically be of the form `v#_wlan%`. For example:

```
./wm vifs add wlan0 1
```

Will add `v1_wlan0` to the list of interfaces.

Each new virtual interface will be used to associate to a different SSID. NetworkManager will automatically bring it up and attempt to associate.

```
ifconfig wlan0 up 
ifconfig v1_wlan0 up 
```

##### Load multichannel driver #####

By using **wm** to add virtual interfaces, a new MAC address is given, derived from the base interface MAC address. The new driver will timeshare between the two interfaces on their respective channels 


```
./wm load
```
 
#### Replace wpa_supplicant ####
```
pkill wpa_supplicant
mv /sbin/wpa_supplicant /sbin/wpa_supplicant.orig
cp ./wpa_supplicant /sbin
```

 
#### Restart network manager ####

```
service network-manager start
```
Both cards should be visible in the GUI, and you should be able to choose an ESSID for each card. 
Associate to various networks on both cards so that passwords are not required later at handoffs. 


#### Set up routing ####

After testing connectivity through each interface and access point, set up [MPTCP routing](http://multipath-tcp.org/pmwiki.php/Users/ConfigureRouting).

The scripts provided in the 'Automatic Configuration' section (mptcp_up and mptcp_down) seem to work fine in Ubuntu Trusty and Mint 17. 


### verify proper MPTCP connectivity 

```
ip ro # shows routes across each virtual interface 
# should answer something like:
# subnet0 dev wlan0 ... src IP0
# subnet1 dev wlan1 ... src IP1

ip ru sh # shows outgoing rules across IP0 and IP1

#verify outside IPs across both networks
curl --interface IP0 http://141.85.37.151/static/ietf/whatip.php
curl --interface IP1 http://141.85.37.151/static/ietf/whatip.php
``` 

#### install measurement script 

This shell script periodically downloads a small file across each interface with regular TCP, and then across both interfaces with MPTCP. 
```
mv measurement_bw.sh /bin
```

* entries to be added to crontab to automate downloading and reporting
```
measurement_bw.sh crontab 
#recommended entries
WLAN0=wlan0
WLAN1=v1_wlan0
0,10,20,30,40,50 * * * * measure_bw.sh start_short $WLAN0   
1,11,21,31,41,51 * * * * measure_bw.sh stop; measure_bw.sh start_short $WLAN1   
2,12,22,32,42,52 * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN0   
4,14,24,34,44,54 * * * * measure_bw.sh stop; measure_bw.sh start_long $WLAN1   
6,16,26,36,46,56 * * * * measure_bw.sh stop; measure_bw.sh start_short multi
7,17,27,37,47,57 * * * * measure_bw.sh stop; measure_bw.sh start_long multi   
9,19,29,39,49,59 * * * * measure_bw.sh stop; measure_bw.sh upload 
```
make sure WLAN0 and WLAN1 reflect actual wireless interfaces

#### data collection

As can be gathered from the above crontab script, data collection means downloading 
2MB chunks every 2 minutes across each interface and using MPTCP. Logs of these transfers
are periodically updated to mobil4.org. Consult the script /sbin/measure_bw.sh for details. 


#### revert laptop to original state 
```
crontab -e 
# and in editor remove all measurement entries

#bring back original supplicant
mv /sbin/wpa_supplicant.orig /sbin/wpa_supplicant 

#remove mptcp kernel 
apt-get remove linux-mptcp 
 
#remove mptcp startup scripts 
rm /etc/network/if-up.d/mptcp_up
rm /etc/network/if-post-down.d/mptcp_down
```

* reboot

### Future updates ###

* Automatic SSID association to ensure "hands-free" operation
* Throughput maximization
* Optimal SSID choice

### Contact ###

* **Vladimir Diaconescu** - vladimirdiaconescu[at]yahoo.com
* **Costin Raiciu** - costin.raiciu[at]cs.pub.ro
* **Dragoș Niculescu** - dragos.niculescu[at]cs.pub.ro
