#!/usr/bin/perl

use warnings;
use strict;
use Test::More tests => 52;

for my $mpm (qw/prefork worker event/) {
    my $res = `omd stop apache`;
    is($?, 0, "apache stopped");

    set_apache_mpm($mpm);

    $res = `omd start apache`;
    is($?, 0, "apache started");

    $res = `curl -qs http://localhost:5000`;
    is($?, 0, "curl worked");

    $res = `curl -qs http://localhost:5000/metrics`;
    is($?, 0, "curl worked");
    like($res, "/apache_requests_total/", "result contains counter");
    like($res, "/apache_server_info/", "result contains apache_server_info");
    like($res, "/Apache\/2.4/", "result contains apache_server_info");
    like($res, "/apache_response_time_seconds.*10\"/", "result contains apache_response_time_seconds");
    like($res, "/apache_response_size_bytes.*10000/", "result contains apache_response_size_bytes");
    like($res, '/mpm="'.$mpm.'"/', "result contains ".$mpm." mpm");

    $res = `curl -qs http://localhost:5000/test`;
    is($?, 0, "curl worked");

    $res = `curl -qs http://localhost:5000/disabled`;
    is($?, 0, "curl worked");

    $res = `curl -qs http://localhost:5000/metrics`;
    is($?, 0, "curl worked");
    like($res, qr(\Qapache_requests_total{application="/test",method="GET",status="404"}\E), "result contains counter with custom label");
    unlike($res, qr(\Q/disabled\E), "result does not contain disabled path counter");

    # test reloads
    $res = `omd reload apache`;
    is($?, 0, "reload worked");

    $res = `curl -qs http://localhost:5000/metrics`;
    is($?, 0, "curl worked");
}

################################################################################
# switch back
set_apache_mpm("prefork");
my $res = `omd restart apache`;
is($?, 0, "apache restarted");

################################################################################
sub set_apache_mpm {
    my($mpm) = @_;
    `sed -e 's/LoadModule mpm_[a-z]*_module/LoadModule mpm_${mpm}_module/g' -e 's/mod_mpm_[a-z]*.so/mod_mpm_${mpm}.so/g' -i etc/apache/apache.conf`;
}