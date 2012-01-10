<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="slax">
  <xsl:variable name="x">
    <message>
      <xsl:value-of select="concat(&quot;Down rev PIC in &quot;, ../name, &quot;, &quot;, name, &quot;: &quot;, description)"/>
    </message>
  </xsl:variable>
  <xsl:variable name="message">
    <x>
      <xsl:value-of select="concat(_name/text(), &quot; (&quot;, id/text(), &quot;)&quot;)"/>
    </x>
  </xsl:variable>
  <xsl:variable name="count" select="concat(/in/one, &quot;-2-&quot;, /in/three, &quot;-4-&quot;, /in/five)"/>
  <xsl:variable name="sp_one" select="&quot;uno, Man&quot;"/>
  <xsl:variable name="sp_two" select="&quot;dos&quot;"/>
  <xsl:variable name="sp_three" select="&quot;tres&quot;"/>
  <xsl:variable name="sp_four" select="&quot;quatro&quot;"/>
  <xsl:variable name="sp_five" select="&quot;cinco&quot;"/>
  <xsl:variable name="is" select="&quot;is&quot;"/>
  <xsl:variable name="zero" select="0"/>
  <xsl:template match="/">
    <out>
      <count>
        <xsl:value-of select="$count"/>
      </count>
      <xsl:variable name="variable" select="&quot;this&quot;"/>
      <xsl:variable name="test" select="($variable =(&quot;this&quot;))"/>
      <test>
        <xsl:value-of select="$test"/>
      </test>
      <xsl:variable name="test2" select="($variable =(concat(&quot;th&quot;, $is)))"/>
      <test>
        <xsl:value-of select="$test2"/>
      </test>
      <output>
        <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:evaluate(&quot;$variable == ('th' _ 'is')&quot;)"/>
      </output>
      <test>
        <xsl:value-of select="concat(substring(&quot;tent&quot;, 1, 3), 6 - 10)"/>
      </test>
      <test>
        <xsl:value-of select="concat($zero + 10, (concat(&quot;th&quot;, $is, (concat(10, 6)))))"/>
      </test>
      <title>
        <xsl:value-of select="concat(&quot;Translation of &quot;, name(), &quot;:&quot;)"/>
      </title>
      <xsl:apply-templates select="in/*"/>
    </out>
  </xsl:template>
  <xsl:template match="*">
    <xsl:element name="{name()}">
      <xsl:value-of xmlns:slax="http://xml.libslax.org/slax" select="slax:evaluate(concat(&quot;$sp_&quot;, name()))"/>
    </xsl:element>
  </xsl:template>
</xsl:stylesheet>
