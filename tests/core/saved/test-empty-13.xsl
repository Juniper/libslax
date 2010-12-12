<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://code.google.com/p/libslax/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:output indent="yes"/>
  <xsl:variable name="slax-indent" mvarname="indent"/>
  <xsl:variable name="indent" select="&quot;aaa&quot;" mutable="yes" svarname="slax-indent"/>
  <xsl:param name="test" select="1"/>
  <xsl:template match="/">
    <top>
      <xsl:call-template name="scalars"/>
      <xsl:call-template name="non-scalars"/>
      <xsl:call-template name="append-scalars"/>
      <xsl:call-template name="to-scalars"/>
      <xsl:variable name="slax-set" mvarname="set"/>
      <xsl:variable name="set" mutable="yes" svarname="slax-set"/>
      <xsl:for-each select="*/*">
        <xsl:if test="starts-with(name() , &quot;i&quot;)">
          <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="set" svarname="slax-set" select="."/>
        </xsl:if>
      </xsl:for-each>
      <set>
        <xsl:copy-of select="$set"/>
      </set>
    </top>
  </xsl:template>
  <xsl:template name="to-scalars">
    <to-scalars>
      <xsl:variable name="slax-var" mvarname="var"/>
      <xsl:variable name="var" select="&quot;one&quot;" mutable="yes" svarname="slax-var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <two>two</two>
      </slax:append-to-variable>
      <xsl:variable name="save" select="$var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;three&quot;"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <four>four</four>
      </slax:append-to-variable>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <save>
        <xsl:copy-of select="$save"/>
      </save>
    </to-scalars>
  </xsl:template>
  <xsl:template name="append-scalars">
    <append-scalars>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-var" mvarname="var">
        <one>one</one>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" mutable="yes" select="slax:mvar-init(&quot;slax-var&quot;)" svarname="slax-var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;two&quot;"/>
      <xsl:variable name="save" select="$var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;three&quot;"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <four>four</four>
      </slax:append-to-variable>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <save>
        <xsl:copy-of select="$save"/>
      </save>
    </append-scalars>
  </xsl:template>
  <xsl:template name="non-scalars">
    <non-scalars>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-var" mvarname="var">
        <one>one</one>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" mutable="yes" select="slax:mvar-init(&quot;slax-var&quot;)" svarname="slax-var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <two>two</two>
      </slax:append-to-variable>
      <xsl:variable name="save" select="$var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <three>three</three>
      </slax:append-to-variable>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <four>four</four>
      </slax:append-to-variable>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <save>
        <xsl:copy-of select="$save"/>
      </save>
    </non-scalars>
  </xsl:template>
  <xsl:template name="scalars">
    <scalar>
      <xsl:variable name="slax-var" mvarname="var"/>
      <xsl:variable name="var" select="&quot;one&quot;" mutable="yes" svarname="slax-var"/>
      <xsl:variable name="save" select="$var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;-and-two&quot;"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="111"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="222"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="333"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="444"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <save>
        <xsl:copy-of select="$save"/>
      </save>
    </scalar>
  </xsl:template>
  <xsl:template name="basic-four">
    <basic-four>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-var" mvarname="var">
        <test>one</test>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" mutable="yes" select="slax:mvar-init(&quot;slax-var&quot;)" svarname="slax-var"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var">
        <test>two</test>
      </slax:set-variable>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;three&quot;"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
      <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="var" svarname="slax-var" select="&quot;four&quot;"/>
      <var>
        <xsl:copy-of select="$var"/>
      </var>
    </basic-four>
  </xsl:template>
  <xsl:template name="simple">
    <simple>
      <xsl:if test="$test">
        <xsl:variable name="slax-one" mvarname="one"/>
        <xsl:variable name="one" select="&quot;111&quot;" mutable="yes" svarname="slax-one"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="one" svarname="slax-one" select="&quot;222&quot;"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="one" svarname="slax-one" select="&quot;333&quot;"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="one" svarname="slax-one" select="&quot;444&quot;"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="one" svarname="slax-one" select="&quot;555&quot;"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <xsl:call-template name="that"/>
        <one>
          <xsl:value-of select="$one"/>
        </one>
        <one>
          <xsl:value-of select="concat($one, $one, $one)"/>
        </one>
        <xsl:variable name="slax-three" mvarname="three"/>
        <xsl:variable name="three" select="&quot;three3&quot;" mutable="yes" svarname="slax-three"/>
        <xsl:variable name="four" select="$three"/>
        <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="three" svarname="slax-three" select="&quot;Third&quot;"/>
        <check three="{$three}" four="{$four}"/>
      </xsl:if>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-five" mvarname="five">
        <a>eh</a>
        <b>bee</b>
        <c>sea</c>
        <d>Dee</d>
        <e>Eh!</e>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="five" mutable="yes" select="slax:mvar-init(&quot;slax-five&quot;)" svarname="slax-five"/>
      <xsl:variable xmlns:xsl="http://www.w3.org/1999/XSL/Transform" name="slax-six" mvarname="six">
        <stuff>
          <a>eh</a>
          <b>bee</b>
          <c>sea</c>
          <d>Dee</d>
          <e>Eh!</e>
        </stuff>
      </xsl:variable>
      <xsl:variable xmlns:slax="http://code.google.com/p/libslax/slax" name="six" mutable="yes" select="slax:mvar-init(&quot;slax-six&quot;)" svarname="slax-six"/>
      <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="five" svarname="slax-five">
        <five>t5</five>
      </slax:set-variable>
      <!-- 
* /
	append $five += "a5";
	/ *
	append $five += "a25";
 -->
      <five>
        <xsl:value-of select="$five"/>
      </five>
      <five>
        <xsl:copy-of select="$five"/>
      </five>
    </simple>
  </xsl:template>
  <xsl:template name="that">
    <xsl:param name="one" select="&quot;five&quot;"/>
    <xsl:variable name="slax-two" mvarname="two"/>
    <xsl:variable name="two" select="&quot;too&quot;" mutable="yes" svarname="slax-two"/>
    <two>
      <xsl:value-of select="$two"/>
    </two>
    <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="two" svarname="slax-two" select="&quot;to&quot;"/>
    <two>
      <xsl:value-of select="$two"/>
    </two>
  </xsl:template>
  <xsl:template name="foo">
    <configuration-text>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="&quot;bbb&quot;"/>
      <xsl:call-template name="other"/>
      <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="&quot;ccc&quot;"/>
    </configuration-text>
  </xsl:template>
  <xsl:template name="other">
    <xsl:param name="depth" select="0"/>
    <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="&quot;ddd&quot;"/>
    <xsl:variable name="tmp" select="&quot;this&quot; + &quot;that&quot;"/>
    <xsl:variable name="save" select="$indent"/>
    <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="&quot;eee&quot;"/>
    <sample>
      <indent>
        <xsl:copy-of select="$indent"/>
      </indent>
      <indent>
        <xsl:value-of select="$indent"/>
      </indent>
      <save>
        <xsl:copy-of select="$save"/>
      </save>
      <save>
        <xsl:value-of select="$save"/>
      </save>
    </sample>
    <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="$save"/>
    <xsl:if test="$depth &lt; 3">
      <xsl:call-template name="other">
        <xsl:with-param name="depth" select="$depth + 1"/>
      </xsl:call-template>
    </xsl:if>
  </xsl:template>
  <xsl:template match="*">
    <xsl:variable name="save" select="$indent"/>
    <line length="{string-length($indent)}">
      <xsl:value-of select="concat(name(), &quot; {&quot;)"/>
    </line>
    <slax:append-to-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="&quot;ddd&quot;"/>
    <xsl:apply-templates/>
    <line>}</line>
    <slax:set-variable xmlns:slax="http://code.google.com/p/libslax/slax" name="indent" svarname="slax-indent" select="$save"/>
  </xsl:template>
  <xsl:template match="text()">
    <!-- nothing -->
  </xsl:template>
</xsl:stylesheet>
