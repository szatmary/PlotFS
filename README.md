# PlotFS
PlotFS is a fuse filesystem for efficient storage of Chia plot files.

PlotFS is not a traditional filesystem. It is mounted read only for farming/harvesting, but all other interactions, such as adding/removing plots, is achieved via the `plotfs` command line tool.

PlotFS writes plot files directly and contiguously to raw block devices, partitions, or files.
Metadata, such as, file offsets and size are recorded in a separate "geometry" file which is stored on the OS drive. Plots can be split into "shards" and spread across multiple disks to achieve maximum storage density.

Please consider dontating:

    xch1hsyyclxn2v59ysd4n8nk577sduw64sg90nr8z26c3h8emq7magdqqzq9n5

# WARNING
This software is beta quality... at best. Use at your own risk. 
The author is not responsible for any loss of data that may occur.

# Project Goals
Modern file systems, while amazing, all have some drawbacks when storing Chia plots.
A filesystem designed specifically for plots will have a much different usage pattern. It must:

* Have zero (or close to zero) space overhead
* Maintain operation upon the failure of any number of disks
* Lose the fewest number of plots possible upon loss of a disk
* Allow for unmacthed disk sizes
* Combine fractional remainder space across disk to maximize the number of plots
* Be fast enough

It doesn't care about:

* Directories
* Frequent deletes
* Write performance
* Large number of small files
* Rendundancy
* Convenience
* Windows
* Humans in general

### Installing

    sudo apt update && sudo apt -y upgrade
    sudo apt install -y git cmake build-essential pkg-config libfuse3-dev libflatbuffers-dev
    git clone https://github.com/szatmary/PlotFS.git
    cd PlotFS && cmake . && make && sudo make install

Note: Ubuntu 20.04 and earlier has a verson of flatc that is too old. Update to a newer Ubuntu (Which you really should do... Your farmer is not mission critical enterprise distributed infrastrcture, and  LTS does not mean its better supported, It means its running old software but still gets security patches) Or, install flatbuffers from source.

    sudo apt purge libflatbuffers-dev
    git clone https://github.com/google/flatbuffers.git && cd flatbuffers && cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release && make && make install

### Updating

    cd PlotFS && git pull && make && sudo make install

### Getting started

Create a place to store our geometry file
    
    sudo mkdir -p /var/local/plotfs
    sudo chown $USER /var/local/plotfs

Create the geometry file

    plotfs --init

This will create the file `/var/local/plotfs/plotfs.bin`

Note: It is VERY important that you back up this file frequently! if you lose this file, you lose your plots!
(It may be possible to rebuild this file later by scanning the dirves/partitions but I have not written a tool to do that yet)

Create a mount point

    sudo mkdir /farm

Mount the filesystem:

    sudo mount.plotfs /farm

Add this directory to chia client. Use the GUI; or from yor chia install directory run: 

     . ./activate && chia plots add -d /farm

Add a disk or partition to the plotfs pool. Repeat for multiple disks/partitions. Pools can be expanded at any time by adding more disks/partitions.
Note: This will erase the data on the disk/partition! 

    sudo plotfs --add_device /dev/disk/by-id/[your disk or partition]

Optional: Configure the filesystem to mount at boot. My personal favorite approach is using cron.

    sudo crontab -e

Add the line

    @reboot /usr/local/bin/mount.plotfs /farm

Start adding plots:

    sudo plotfs --add_plot /path/to/[plot file.plot]

## plotfs CLI usage

$ plotfs 

--init

    Initialize the plotfs filesystem. Creates a file at `/var/local/plotfs/plotfs.bin`. Back this file up!
    combine with --force to clear the entire geometry file and start over.

--list_devices

    List the devices that are currently being used by plotfs.

--add_device [device path] 

    Add a new device or partition to the filesystem.
    Combine with --force to erase and reuse an existing device or partition.

--remove_device [device id]

    Remove a device or partition from the filesystem.

--list_plots

    List the plots that are currently stored in the filesystem.

--add_plot [plot path]

    Add a plot to the filesystem.

--remove_plot [plot id]

    Remove a plot from the filesystem.

--remove_source

    When used with --add_plot will remove the file located at [plot path] if the plot is added successfully.

$ mount.plotfs [mount point]

    Mounts the filesystem at the given mount point.

## FAQ

Q. Wow this is great! How can I give you all my Chia?

A. xch1hsyyclxn2v59ysd4n8nk577sduw64sg90nr8z26c3h8emq7magdqqzq9n5

Q. Will this work on Raspberry Pi?

A. I haven't tested yet, but it should. If it doesn't let me know, I'll fix it.

Q. Why do the filenames not have the date?

A. Plotfs does not have the concept of filenames. It uses the plot id and k value from the plot header as the file name. The plot header does not record the date created.

Q. What happens when/if chia releases plot compression?

A. You can compress a plot, then delete and add it back via the cli.. To minimize fragmentation it's best to do this in a specific order. I will document more if/when plot compression is available.

Q. I lost my config file! Can you release the recovery tool?

A. I haven't written it yet. PlotFS is just a side project and my day job keeps me busy.
    Greasing the wheels wouldn't hurt though: xch1hsyyclxn2v59ysd4n8nk577sduw64sg90nr8z26c3h8emq7magdqqzq9n5

Q. Windows when?

A. Literally never. Use linux.

Q. How can I get in contact with you?

A. email: matt@szatmary.org twitter: @m3u8
