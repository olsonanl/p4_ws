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

test_get_download_url($ua, $url, $token);

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
