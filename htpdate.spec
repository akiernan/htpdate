Summary: HTTP based time synchronization tool
Name: htpdate
Version: 0.8.5
Release: 1
License: GPL
Group: System Environment/Daemons
URL: http://www.clevervest.com/htp/
Packager: Eddy Vervest <eddy@clevervest.com>
Source: http://www.clevervest.com/htp/archive/c/%{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-root


%description
The HTTP Time Protocol (HTP) is used to synchronize a computer's time
with web servers as reference time source. Htpdate will synchronize your
computer's time by extracting timestamps from HTTP headers found
in web servers responses. Htpdate can be used as a daemon, to keep your
computer synchronized.
Accuracy of htpdate is usually better than 0.5 seconds (even better with
multiple servers). If this is not good enough for you, try the ntpd package.

Install the htp package if you need tools for keeping your system's
time synchronized via the HTP protocol. Htpdate works also through
proxy servers.

%prep
%setup -q

%build
make
strip -s htpdate

%install
mkdir -p %{buildroot}/etc/rc.d/init.d
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/man/man8

install -m 755 htpdate %{buildroot}/usr/bin/htpdate
install -m 644 htpdate.8.gz %{buildroot}/usr/share/man/man8/htpdate.8.gz
install -m 755 htpdate.init %{buildroot}/etc/rc.d/init.d/htpdate

%clean
[ "%{buildroot}" != "/" ] && rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc README CHANGES
%config(noreplace) /etc/rc.d/init.d/htpdate
/usr/bin/htpdate
/usr/share/man/man8/htpdate.8.gz
