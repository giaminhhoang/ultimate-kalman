# UltimateKalman: Flexible Kalman Filtering and Smoothing Using Orthogonal Transformations

This repository contains the source code of UltimateKalman in three different programming languages: MATLAB, C and Java.

Most of the documentation for UltimateKalman is available in an article [available on arXiv](https://arxiv.org/abs/2207.13526).

## Building and Testing the Code

Testing the MATLAB version is trivial. Launch MATLAB, make sure that you are in the matlab directory
of this project (or that it is in MATLAB's search path), and run
`replication`.
MATLAB will run a number
of tests and will produce the graphs in the article, except for the performance graphs. 

To build the Java version, run the Windows batch script 
`build.bat`.
It should build an archive file called `ultimatekalman.jar`. You need to have a the Java
command line tools of the JDK (jar and javac) on your path for this script to work. To test the Java version, after
you have built it, run in MATLAB
`replication('Java')`.
It should produce exactly the same output as the MATLAB version.

To build the C version, run the MATLAB script
`compile`.
It will compile the C version into a MATLAB-callable dynamic link library (a mex file).
To test the C version, run in MATLAB
`replication('C')`.
Again it should produce the same graphs.

Once you have built all three versions, you can produce the performance graphs shown in the 
article by running in MATLB
`replication('MATLAB',false,true)`.
    
That's it!

To use the Java version with client code other than the MATLAB adapter class, simply include
`ultimatekalman.jar` and Apache Commons Math in the class path (this software comes with a particular
version of the Apache Commons Math library, `commons-math3-3.6.1.jar`).

To use the C version with client code other than the MATLAB adapter class, add to your project a single
C file, `ultimatekalman.c`, and a single header file, `ultimatekalman.h`.

## License

Copyright 2020-2022 Sivan Toledo.
 
 UltimateKalman is free software; you can redistribute it and/or modify
    it under the terms of either:

 the GNU Lesser General Public License as published by the Free
        Software Foundation; either version 3 of the License, or (at your
        option) any later version.

or

the GNU General Public License as published by the Free Software
        Foundation; either version 2 of the License, or (at your option) any
        later version.

or both in parallel, as here, 
    WITH THE ADDITIONAL REQUIREMENT 
    that if you use this software or derivatives of it, directly or indirectly, to produce
    research that is described in a research paper, you need to cite the most
    up-to-date version of the article that describes UltimateKalman in your paper.
    
Currently, the version to cite is [the version on arXiv](https://arxiv.org/abs/2207.13526).

UltimateKalman is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

You should have received copies of the GNU General Public License and the
    GNU Lesser General Public License along with this software.  If not,
    see https://www.gnu.org/licenses/.
    