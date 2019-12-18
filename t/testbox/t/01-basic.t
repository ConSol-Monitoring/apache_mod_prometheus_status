#!/usr/bin/perl

use warnings;
use strict;
use Test::More tests => 4;

my $res = `curl -qs http://localhost:5000/metrics`;
is($?, 0, "curl worked");
like($res, "/apache_requests_total/", "result contains counter");
like($res, "/apache_server_info/", "result contains apache_server_info");
like($res, "/Apache\/2.4/", "result contains apache_server_info");
