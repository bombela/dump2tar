# dump2tar

A (for now prototype) tool to convert a NetApp dump backup to a tar archive.

## Usage

```shell
$ dump2tar < input.dump > output.tar
```

## How it works

A dump is a BSD disk dump with a bunch of inodes. Think of it as a simplified
inode based filesystem without unused blocks.

NetApp ensures that directories inodes (stage 3) are always written before file inodes (stage 4).
See documentation: https://library.netapp.com/ecmdocs/ECMP1368865/html/GUID-34EFEE5F-E97D-4CAA-8E7E-93AE65E486D9.html.

This makes it possible to build the file tree in memory and output the tar
archive on the fly.

## Future work

### File types

The support for the following file types is not implemented:

 - symlinks
 - fifo
 - dev files

### ACLs

The dump file contains the Access Control Lists (ACLs) at the end (stage 5). It
should be possible to understand the format and append a `setfacl --restore`
compatible file to the tar output.

## Contact and copyright

François-Xavier Bourlet bombela@gmail.com

Copyright 2016 Google Inc. All Rights Reserved. APACHE License Version 2.0.

## Disclaimer

Although this project is owned by Google Inc. this is not a Google supported or
affiliated project.
