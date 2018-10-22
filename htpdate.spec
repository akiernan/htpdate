Summary: HTTP based time synchronization tool
Name: htpdate
Version: 0.3
Release: 1
License: GNU General Public License version 2
Group: System Environment/Daemons
URL: http://www.clevervest.com/htp/
Packager: Eddy Vervest <Eddy@cleVervest.com>
Source: http://www.clevervest.com/htp/archive/c/%{name}-%{version}.tar.bz2
BuildRoot: %{_tmppath}/%{name}-%{version}-root


%description
The HTTP Time Protocol (HTP) is used to synchronize a computer's time
with web servers as reference time source. Htp will synchronize your
computer's  time to Greenwich Mean Time (GMT) via HTTP headers from web
servers. The htpdate package includes a program for retrieving the
date and time from remote machines via a network. Htpdate works through
proxy servers. Accuracy of htpdate will be within 0.5  seconds  (better
with multiple servers). If this is not good enough for you, try the
ntpd package.

Install the htp package if you need tools for keeping your system's
time synchronized via the HTP protocol. Htp works also through
proxy servers.

%prep
%setup -q

%build
make
strip -s htpdate

%install
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/man/man8

install -m 755 htpdate %{buildroot}/usr/bin/htpdate
install -m 644 htpdate.8.gz %{buildroot}/usr/man/man8/htpdate.8.gz

%clean
[ "%{buildroot}" != "/" ] && rm -rf %{buildroot}

%files
%defattr(-,root,root)
%doc README
/usr/bin/htpdate
/usr/man/man8/htpdate.8.gz
