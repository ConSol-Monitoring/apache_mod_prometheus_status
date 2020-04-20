#!/usr/bin/perl

use warnings;
use strict;
use Test::More tests => 11;

my $res = `curl -qs http://localhost:5000/metrics`;
is($?, 0, "curl worked");
like($res, "/apache_requests_total/", "result contains counter");
like($res, "/apache_server_info/", "result contains apache_server_info");
like($res, "/Apache\/2.4/", "result contains apache_server_info");
like($res, "/apache_response_time_seconds.*10\"/", "result contains apache_response_time_seconds");
like($res, "/apache_response_size_bytes.*10000/", "result contains apache_response_size_bytes");


$res = `curl -qs http://localhost:5000/test`;
is($?, 0, "curl worked");

$res = `curl -qs http://localhost:5000/disabled`;
is($?, 0, "curl worked");

$res = `curl -qs http://localhost:5000/metrics`;
is($?, 0, "curl worked");
like($res, qr(\Qapache_requests_total{application="/test",method="GET",status="404"}\E), "result contains counter with custom label");
unlike($res, qr(\Q/disabled\E), "result does not contain disabled path counter");
