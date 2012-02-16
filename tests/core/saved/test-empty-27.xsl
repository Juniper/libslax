<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:redirect="org.apache.xalan.xslt.extensions.Redirect" xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="redirect slax-ext slax">
  <xsl:variable name="test-temp-1">
    <x>ManManMan</x>
    <x>Fish Sauce</x>
    <x>amazingly awesome sizabilities</x>
  </xsl:variable>
  <xsl:variable xmlns:slax-ext="http://xmlsoft.org/XSLT/namespace" name="test" select="slax-ext:node-set($test-temp-1)"/>
  <xsl:template match="/">
    <out>
      <in>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:base64-encode(&quot;ManManMan&quot;)"/>
      </in>
      <out>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:base64-decode(&quot;TWFuTWFuTWFu&quot;)"/>
      </out>
      <xsl:variable name="slax-dot-1" select="."/>
      <xsl:for-each select="$test/x">
        <xsl:variable name="in" select="."/>
        <xsl:for-each select="$slax-dot-1">
          <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="enc" select="slax:base64-encode($in)"/>
          <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="dec" select="slax:base64-decode($in)"/>
          <test>
            <in>
              <xsl:value-of select="$in"/>
            </in>
            <enc>
              <xsl:value-of select="$enc"/>
            </enc>
            <dec>
              <xsl:value-of select="$dec"/>
            </dec>
          </test>
        </xsl:for-each>
      </xsl:for-each>
      <redirect:write href="hello.txt" method="text">
        <xsl:text>Hello, World!
</xsl:text>
      </redirect:write>
      <test>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:document(&quot;hello.txt&quot;)"/>
      </test>
      <redirect:write href="hello.txt" method="text">
        <xsl:text>TWFuTWFuTWFu</xsl:text>
      </redirect:write>
      <xsl:variable name="opts">
        <format>base64</format>
        <encoding>utf-8</encoding>
        <non-xml>X</non-xml>
      </xsl:variable>
      <test2>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:document(&quot;hello.txt&quot;, $opts)"/>
      </test2>
      <redirect:write href="hello.txt" method="text">
        <xsl:text>AQwQaAECBXkgc291EAoGAQwMBQ4K</xsl:text>
      </redirect:write>
      <test3>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:document(&quot;hello.txt&quot;, $opts)"/>
      </test3>
      <xsl:variable name="opts2">
        <format>base64</format>
        <encoding>utf-8</encoding>
        <non-xml>က</non-xml>
      </xsl:variable>
      <test3>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:document(&quot;hello.txt&quot;, $opts2)"/>
      </test3>
    </out>
  </xsl:template>
</xsl:stylesheet>
