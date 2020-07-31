use strict;
use LWP::UserAgent;
use File::Slurp;
use JSON::XS;

my $json = JSON::XS->new->pretty(1);

my $jsonrpc_id = 1;

my $token = read_file("$ENV{HOME}/.patric_token");
chomp $token;

my $ua = LWP::UserAgent->new();

my $url = 'http://holly:12312/api';

test_delete($ua, $url, $token);
#test_copy($ua, $url, $token);
#test_set_permissions($ua, $url, $token);
#test_update_metadata($ua, $url, $token);
#test_create($ua, $url, $token);
#test_get_download_url($ua, $url, $token);

sub test_get_download_url
{
    my($ua, $url, $token) = @_;

    my @objs = ('/olson@patricbrc.org/home/toy1.fq',
		'/olson@patricbrc.org/home/test/tree1',
		'/olson@patricbrc.org/home/Genome Groups/ default ');

    for my $obj (@objs)
    {
	test_call($ua, $url, $token,
		  "Workspace.get_download_url",
		  [{objects => [$obj]}]);
    }

}

sub test_copy
{
    my($ua, $url, $token) = @_;

    my @p = ($ua, $url, $token);
    
    my $from_invalid = '/olso';
    
    my $from = '/olson@patricbrc.org/xxx/colicin.calls.txt';
    my $from_bad = '/olson@patricbrc.org/xxx/colicin.calls.txtxx';
    my $to = '/olson@patricbrc.org/xxx/colicin.calls.copy.txt';
    my $xxx = '/olson@patricbrc.org/xxx/yz';
    my $t1 = '/olson@patricbrc.org/home/workshop';

    my $t2 = '/olson@patricbrc.org/home/test';

    if (0)
    {
	do_copy(@p, $from_invalid, $to);
	
	do_copy(@p, $from_bad, $to);
	
	do_copy(@p, $xxx, $from);
	do_copy(@p, $from, $from);
    }
    do_copy(@p, $t2, $xxx, 0, 1);
}

sub do_copy
{
    my($ua, $url, $token, $from, $to, $overwrite, $recursive) = @_;
    test_call($ua, $url, $token,
	      "Workspace.copy",
	      [{objects => [ [ $from, $to ] ],
		    overwrite => ($overwrite ? 1 : 0),
		    recursive => ($recursive ? 1 : 0),
		    move => 0,
		    adminmode => 0
		    }]);


}

sub test_delete
{
    my($ua, $url, $token) = @_;

    my @p = ($ua, $url, $token);
    
    my $from_invalid = '/olso';
    
    my $xxx = '/olson@patricbrc.org/xxx/yz/test1/test2';
    do_delete(@p, $xxx, 1, 1);
}

sub do_delete
{
    my($ua, $url, $token, $path, $deleteDir, $force) = @_;
    test_call($ua, $url, $token,
	      "Workspace.delete",
	      [{objects => [ $path ],
		    deleteDirectories => ($deleteDir ? 1 : 0),
		    force => ($force ? 1 : 0),
		    adminmode => 0
		    }]);


}


sub test_update_metadata
{
    my($ua, $url, $token) = @_;

    my @objs = (
		# '/olson@patricbrc.org/xxx',
		'/olson@patricbrc.org/home/toy1.fq',
		);

    for my $obj (@objs)
    {
	test_call($ua, $url, $token,
		  "Workspace.update_metadata",
		  [{objects => [
				# [$obj],
				[$obj, { c => 13 }],
				#[$obj, { a => 11 }, "txt" ],
				# [$obj, undef, "reads"],
				#[$obj, undef, "folder"],
				# [$obj, undef, undef, "2020-01-02T03:31:12Z"],
				],
			append => 1,
		    }]);
	if (0) {
	    test_call($ua, $url, $token,
		      "Workspace.update_metadata",
		      [{objects => [
				    [$obj, {}, "newtype"],
				    ],
			}]);
	}
    }

}

sub test_set_permissions
{
    my($ua, $url, $token) = @_;

    my @objs = (
		'/olson@patricbrc.org/xxx',
		# '/olson@patricbrc.org/home/toy1.fq',
		);

    for my $obj (@objs)
    {
	test_call($ua, $url, $token,
		  "Workspace.set_permissions",
		  [{ path => $obj,
			 permissions => [['olson@patricbrc.org', 'r'], ['foobar', 'o']],
			 new_global_permission => 'n',
			 adminmode => 0,
		      
		    }]);
	test_call($ua, $url, $token,
		  "Workspace.set_permissions",
		  [{ path => $obj,
			 permissions => [['olson@patricbrc.org', 'w'], ['foobar', 'n']],
			 new_global_permission => 'r',
			 adminmode => 0,
		      
		    }]);
    }

}

sub test_create
{
    my($ua, $url, $token) = @_;

    my @objs = (
#		['/olson@patricbrc.org/home/test', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/home/test', 'reads', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/home/newtest', 'txt', { a => 34, b => 3.14, c => "some data"}, "My Data\n"],
#		['/olson@patricbrc.org/home/newtestfld', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/xxx/toy1.fq', 'reads', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/home/toy1.fq', 'folder', { a => 34, b => 3.14, c => "some data" }],
		['/olson@patricbrc.org/home/toy1.fq/xxx', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/xxx/yyy/zzz/aaa/bbb', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/olson@patricbrc.org/xxx/abc', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/olsonx@patricbrc.org/xxx', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/abcde', 'folder', { a => 34, b => 3.14, c => "some data" }],
#		['/', 'folder', { a => 34, b => 3.14, c => "some data" }],
		);

    for my $obj (@objs)
    {
	test_call($ua, $url, $token,
		  "Workspace.create",
		  [{objects => [$obj], createUploadNodes => 0, overwrite => 0  }]);
#		  [{objects => [$obj], setowner => "dahlia", adminmode=>1 }]);
    }

}

sub test_call
{
    my($ua, $url, $token, $method, $params) = @_;

    
    my $req = {
	id => $jsonrpc_id++,
	method => $method,
	params => $params,
    };
    my $enc_req = $json->encode($req);

    my $res = $ua->post($url,
			Authorization => $token,
			Content => $enc_req);
    if ($res->is_success)
    {
	my $txt = $res->content;
	my $dec = $json->decode($txt);
	print $json->encode($dec);
    }
    else
    {
	print STDERR "Failure: " . $res->status_line . " " . $res->content . "\n";
    }
	
}
