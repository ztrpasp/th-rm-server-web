# About 
CoLoR packet manipulation library.

# Prerequisites
`scapy`, `wheel` and `setuptools`
```
pip3 install wheel
pip3 install setuptools
```

# Create wheel file
Execute the following command. `color*.whl` will be generated in directory `dist/`.
```
python3 setup.py bdist_wheel
```

# Install
If you want to install this package to your system, use command
```
pip3 install <color*.whl>
```
or
```
sudo python3 setup.py install
```
color*.whl is the file created by `python3 setup.py bdist_wheel`.

# Usage
Examples are given in Docstring.
```
>>> from color_packet import *
>>> help(DATA)
>>> help(GET)
>>> help(PIDGen)
```