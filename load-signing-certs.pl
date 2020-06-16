
#
# Download the given list of signing certificates and create
# C++ module to store them.
#

use Template;
use Data::Dumper;
use JSON::XS;
use Template;
use strict;
use LWP::UserAgent;

my @urls = qw(https://nexus.api.globusonline.org/goauth/keys https://user.alpha.patricbrc.org/public_key https://user.beta.patricbrc.org/public_key https://user.patricbrc.org/public_key http://rast.nmpdr.org/goauth/keys/E087E220-F8B1-11E3-9175-BD9D42A49C03 https://rast.nmpdr.org/goauth/keys/E087E220-F8B1-11E3-9175-BD9D42A49C03);

my $ua = LWP::UserAgent->new;

my %vars;

my $certs = $vars{certs} = [];
my $rsa_certs = $vars{rsa_certs} = [];

for my $url (@urls)
{
    my $res = $ua->get($url);
    if (!$res->is_success)
    {
	warn "Failed to get $url: " . $res->status_line . "\n";
	next;
    }
    my $txt = $res->content;
    my $doc = decode_json($txt);
    my $key = $doc->{pubkey};
    $key =~ s/\n/\\n/g;
    $key =~ s/\t/\\t/g;

    my $ent = { url => $url, text => $key };
    if ($key =~ /RSA/)
    {
	$ent->{func} = 'PEM_read_bio_RSAPublicKey';
    }
    else
    {
	$ent->{func} = 'PEM_read_bio_RSA_PUBKEY'
    }
    push(@$certs, $ent);
}

my $templ = Template->new(ABSOLUTE => 1);
my $ok = $templ->process("SigningCerts.h.tt", \%vars, 'SigningCerts.h');
$ok or die "Template failed: " . $templ->error() . "\n";
