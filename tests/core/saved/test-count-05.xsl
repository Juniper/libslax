<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="last" select="document(&quot;test-count.xml&quot;)"/>
  <xsl:variable name="slax-one" mvarname="one"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="one" select="slax:mvar-init(&quot;one&quot;, &quot;slax-one&quot;, $slax-one, $last/in)" mutable="yes" svarname="slax-one"/>
  <xsl:variable name="slax-two" mvarname="two"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="two" select="slax:mvar-init(&quot;two&quot;, &quot;slax-two&quot;, $slax-two, $last/in/node())" mutable="yes" svarname="slax-two"/>
  <xsl:template match="/">
    <top>
      <one>
        <before>
          <xsl:copy-of select="$one"/>
        </before>
        <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="one" svarname="slax-one">
          <new>one</new>
        </slax:append-to-variable>
        <after>
          <xsl:copy-of select="$one"/>
        </after>
      </one>
      <two>
        <before>
          <xsl:copy-of select="$two"/>
        </before>
        <slax:append-to-variable xmlns:slax="http://xml.libslax.org/slax" name="two" svarname="slax-two">
          <new>one</new>
        </slax:append-to-variable>
        <after>
          <xsl:copy-of select="$two"/>
        </after>
      </two>
    </top>
  </xsl:template>
</xsl:stylesheet>
